# StarForgeOS — Phase 2: receive and apply an OTA update

**Audience:** the coding agent working in this repository.
**Goal:** the timer takes an AWS IoT Job, downloads the firmware image, verifies
its SHA-256, flashes the inactive slot, reboots into it, and reports the result —
unattended, over its brief Wi-Fi session.

Read this fully first. Sections 1 and 2 contain constraints that override the
obvious implementation.

---

## 1. The situation this code runs in

- The device is powered on for **minutes**, typically the day before a race, then
  hard-powered-off. It may be dark for weeks.
- A job may have been queued days ago. That is fine: the URL inside the job
  document is **generated when the device fetches the document**, so it is fresh
  at the moment of use. Nothing expires while the device sleeps.
- There is **no clean shutdown**. Anything that must survive must be in NVS or on
  flash before the user pulls power.
- Wi-Fi is opt-in and may be absent. The device must behave identically to today
  when it cannot reach AWS.

## 2. Hard constraints

1. **Never interrupt race timing.** Downloading and flashing must not run while a
   race is active. Defer, like the Wi-Fi join does.
2. **Never block the Arduino loop.** All of this runs inside the existing AWS
   task (core 0), sequentially after MQTT work.
3. **The update must survive a reboot, and a failed update must leave the device
   bootable.** The bootloader rolls back if the new app never marks itself valid
   — which means the new app **must** call `esp_ota_mark_app_valid_cancel_rollback()`
   after a healthy start. Miss this and every successful update silently reverts
   on the next power cycle.
4. **Do not update to the version already running.** Compare first; report the job
   as `SUCCEEDED` with a note rather than re-flashing identical firmware.
5. Every job execution must reach a terminal state, or IoT Core keeps re-offering
   the same job forever.

## 3. Flow

```
notify-next (subscribed, QoS 1)
   └─▶ if a race is active: leave it queued, return
   └─▶ fetch the document:  $aws/things/<thing>/jobs/<jobId>/get
   └─▶ report IN_PROGRESS (statusDetails: {"step":"starting"})
   └─▶ if document.version == SF_FW_VERSION: report SUCCEEDED {"note":"already-current"} and stop
   └─▶ GET the document URL (plain HTTPS; the presigned URL needs no auth headers)
   └─▶ stream to the inactive OTA slot in 4 KB chunks, updating an mbedtls SHA-256 as it goes
   └─▶ hash != document.sha256 → esp_ota_abort(), report FAILED {"reason":"hash_mismatch"}
   └─▶ esp_ota_end + esp_ota_set_boot_partition → report SUCCEEDED {"version":<new>}
   └─▶ reboot after ~3 s (long enough for the PUBACK of the report)
```

On the next boot into the new image: if init reaches healthy (a short delay plus
the existing setup completing), call `esp_ota_mark_app_valid_cancel_rollback()`.
Publishing the new `fw_version` to the shadow then closes the loop.

## 4. Implementation requirements

**Streaming, not buffering.** The image is ~1.5 MB. Use `esp_http_client` with
event handling (or `esp_https_ota`'s low-level pieces) and write 4 KB chunks via
`esp_ota_write`, feeding the same chunk to `mbedtls_sha256_update`. Do not
allocate the whole image in RAM, and do not write it to SPIFFS (the filesystem
partition is the same size as the app).

**TLS.** The download host is S3. Use the existing credential files: the
`root-CA.crt` already on SPIFFS validates it. Do not disable verification.

**Timeouts and bounds.** Per-session single attempt. Bound the whole operation
(e.g. 5 minutes) and bound the connect phase separately (10 s). On timeout:
`esp_ota_abort()`, report `FAILED {"reason":"timeout"}`, leave the device
running its current firmware. **No cross-session resume in this phase** — a
retry starts from byte 0 on the next boot. Record that decision in a comment so
nobody assumes otherwise; resumed transfers are a later concern once real
failure data exists.

**Progress.** Log a line every 25 % (`download 50% (748 KB / 1499 KB)`). No LCD UI
in this phase.

**Failure reasons** — short, stable strings the cloud can aggregate:
`no_url`, `http_status`, `hash_mismatch`, `ota_begin_failed`,
`ota_write_failed`, `image_invalid`, `timeout`, `reboot_pending`.

**Reporting topic and shape:**

```
$aws/things/<thing>/jobs/<jobId>/update      QoS 1
{"status":"IN_PROGRESS","statusDetails":{"step":"downloading","pct":50}}
{"status":"SUCCEEDED","statusDetails":{"version":"2.0.1","reboot":true}}
{"status":"FAILED","statusDetails":{"reason":"hash_mismatch","pct":83}}
```

The IoT policy already permits publish and subscribe on
`$aws/things/<thing>/jobs/*`, so no policy change is needed — verify rather than
assume, and say so in your report.

## 5. Non-goals for Phase 2

- **No signature verification.** SHA-256 only. Signature checking needs the
  public key embedded and `esp_secure_boot_verify_signature_block()` wired into
  the boot path — Phase 3, where a deliberately corrupt image must be rejected.
- No cross-session resume.
- No rollback job handling, no rings or thresholds.
- No LCD or web UI for update status.
- No changes on non-touch boards; everything guarded by `BOARD_ESP32_S3_TOUCH`.

## 6. The artifact must come from this branch

The release `v2.0.1` on GitHub was built from `main`, which predates the AWS
client — its image has no MQTT code at all. Flashing it makes the device silent.

So for testing: bump `SF_FW_VERSION` in `src/config/config.h` to the new value,
build `-e esp32-s3-touch`, package the usual bundle, and publish **from this
branch** as a pre-release carrying `ota.json`. The image that receives the
update must be the image that contains this code.

## 7. Acceptance criteria

1. `pio run -e esp32-s3-touch` and `pio run -e esp32-s3` both build.
2. With no job queued: nothing happens, session behaves exactly as before.
3. With a job queued and a race running: the job stays queued; no download starts.
4. With a job queued and idle: serial shows `job <id>: IN_PROGRESS`,
   progress lines, then `SUCCEEDED`; `aws iot list-job-executions-for-job` shows
   the execution terminal and `SUCCEEDED`.
5. The device reboots into the new version and the shadow reports the new
   `fw_version`. **A second power cycle keeps the new version** — this proves
   `esp_ota_mark_app_valid_cancel_rollback()` ran.
6. Deliberately corrupting the stored `sha256` in the job document produces
   `FAILED {reason: hash_mismatch}` and the device still runs its old firmware.
7. `git status` clean of credential files.

## 8. Reporting back

Report: files created/modified, how the streaming write and hash are wired, what
happens on each failure path, where `esp_ota_mark_app_valid_cancel_rollback()`
is called and why there, and which acceptance rows you verified on hardware
versus reasoned about. Flag anything in section 2 you could not satisfy.

Do not commit. Do not push. Show the diff.
