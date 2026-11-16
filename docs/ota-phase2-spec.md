# StarForgeOS — Phase 2: receive and apply an OTA update

**Audience:** the coding agent working in this repository.
**Revision 2** — the update is now **user-initiated**. Revision 1 assumed the
device applied a job unattended as soon as it connected; that is superseded.
See §3.

Read this fully first. Section 2 contains constraints that override the obvious
implementation, and section 4 states a memory budget that the code must respect
at link time, not just at run time.

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
- The board has ~8 MB PSRAM but a **small, fragmented internal heap**. The AWS
  and OTA code competes with LVGL, the web server and lwIP for internal RAM.

## 2. Hard constraints

1. **Checking is passive.** The version check must not interrupt, delay or alter
   race timing. It is allowed to be skipped entirely when memory is tight.
2. **Applying is explicit and destructive.** Nothing is downloaded or flashed
   until the user asks. Once they ask, the current session is over: timing stops,
   in-RAM laps are discarded, and the device ends in a reboot. This is acceptable
   and intended — the user chose it.
3. **Only one TLS session may ever be live.** The MQTT client must be torn down
   before the HTTP download begins. mbedTLS needs ~32 KB of *contiguous internal*
   RAM per handshake and there is not room for two. This is a design constraint,
   not an optimisation.
4. **The check path allocates nothing OTA-sized.** No multi-kilobyte static
   buffers, no image-sized allocation. Anything the apply path needs is
   allocated from PSRAM when it starts, and freed (or abandoned to the reboot)
   when it ends.
5. **Never block the Arduino loop.** All of this runs inside the existing AWS
   task (core 0).
6. **The update must survive a reboot, and a failed update must leave the device
   bootable.** The new app **must** call
   `esp_ota_mark_app_valid_cancel_rollback()` after a healthy start. Miss this
   and every successful update silently reverts on the next power cycle.
7. **Do not offer the version already running.** Compare first. If they match,
   report `SUCCEEDED {"note":"already-current"}` and show no prompt.
8. **A job stays `QUEUED` until the user applies it.** Do not report
   `IN_PROGRESS` at check time. `QUEUED` has no in-progress timeout, so an
   update offered today is still offered when the timer is pulled out of a drawer
   weeks later. `IN_PROGRESS` begins only when the user commits.

## 3. Two modes

### Check — runs every session, cheap

```
shadow publish (existing)
   └─▶ subscribe jobs/notify-next, publish jobs/$next/get
   └─▶ if no job: nothing. Idle.
   └─▶ fetch the document:  $aws/things/<thing>/jobs/<jobId>/get
   └─▶ document.version == SF_FW_VERSION → report SUCCEEDED {"note":"already-current"}
   └─▶ otherwise: parse the version, remember the job ID, show the prompt.
                  Execution stays QUEUED. No download. No allocation.
```

The presigned URL inside the document is **ignored at check time**. Do not copy
it into a static buffer (see §4). The document is parsed for `jobId`, `version`
and `sha256` only.

### Apply — user-initiated, exclusive

```
user taps install (deliberate two-step confirm, see §6)
   └─▶ stop the timing loop; the session is forfeit
   └─▶ report IN_PROGRESS {"step":"starting"}
   └─▶ destroy the MQTT client  (frees the TLS session and its buffers)
   └─▶ allocate the PSRAM chunk buffer and SHA-256 context
   └─▶ re-fetch the document for a fresh presigned URL   ← see note below
   └─▶ GET the URL, stream to the inactive slot in 4 KB chunks, updating SHA-256
   └─▶ hash mismatch → esp_ota_abort(), report FAILED {"reason":"hash_mismatch"}
   └─▶ esp_ota_end + esp_ota_set_boot_partition → report SUCCEEDED {"version":<new>}
   └─▶ reboot after ~3 s (long enough for the PUBACK of the report)
```

**Note on re-fetching the document.** The URL is minted when the document is
fetched, so a URL fetched at check time may be stale by the time the user taps
install — minutes later, or after the timer sat idle. Re-fetch after the MQTT
client is back up for the apply path, or accept a short-lived URL and re-fetch on
`http_status`. Simplest correct sequence: reconnect MQTT, fetch document, destroy
MQTT, download. State the ordering you chose in a comment.

**Reporting after the MQTT teardown.** If the client must be destroyed before the
download, the `SUCCEEDED` report has to happen after the download — which means
bringing MQTT back up for it (a fresh handshake), or relying on the new image to
report on next boot. Whichever you choose: do not leave the execution
`IN_PROGRESS` after a successful flash. The next boot's `already-current` report
is a valid way to close it, as long as it actually happens.

On the next boot into the new image: after setup completes, call
`esp_ota_mark_app_valid_cancel_rollback()`. Publishing the new `fw_version` to the
shadow closes the loop.

## 4. Memory budget

This section exists because the device already fails with
`esp-tls: create_ssl_handle failed` / `mbedtls_ssl_setup returned -0x7F00`
(`MBEDTLS_ERR_SSL_ALLOC_FAILED`) on the MQTT connect of the check path. The
budget below is what makes that go away.

**Instrument first, then change.** Log, in `BOARD_ESP32_S3_TOUCH` builds only:

| point | values |
|---|---|
| before and after the MQTT connect | `heap_caps_get_free_size(MALLOC_CAP_INTERNAL)`, `heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)`, `heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)`, `heap_caps_get_free_size(MALLOC_CAP_SPIRAM)` |
| on `MQTT_EVENT_ERROR` / disconnect | same four |
| at apply start and after `esp_ota_end` | same four |

`largest_free_block` is the number that matters — 70 KB free with 20 KB largest
fails exactly like a shortage.

**Required changes:**

| change | intent |
|---|---|
| `cfg.buffer.size` 8192 → **3072**, `out_size` 2048 → **1024** | 3 KB holds a job document carrying a ~900-byte presigned URL; 1 KB is ample for status publishes |
| delete `static char s_url[3072]` | the URL lives in the receive buffer; copy it only at apply time, into a PSRAM allocation |
| `s_asm_body` and the 4 KB `chunk` → PSRAM at apply time, not `.bss` | ~7 KB of internal RAM returned at link time |
| `aws_task` stack: keep 24576 for now | revisit only if the numbers say so; TLS needs real stack |

**Treat `-0x7F00` at check time as transient, never fatal.** Log it, retry after
the current race ends or on the next session, and **do not** report `FAILED` to
AWS for it. A job marked FAILED because of a transient internal-heap shortage
makes a good artifact look broken.

## 5. Implementation requirements for the apply path

**Streaming, not buffering.** The image is ~1.6 MB. Use `esp_http_client` and
write 4 KB chunks via `esp_ota_write`, feeding the same chunk to
`mbedtls_sha256_update`. Do not allocate the image in RAM, do not write it to
SPIFFS (the filesystem partition is the same size as the app).

**TLS.** The download host is S3. Use the `root-CA.crt` already on SPIFFS. Do not
disable verification. Send no Authorization header; the presigned URL carries its
own authorisation. Do not log the URL.

**Timeouts.** One attempt per apply. Bound the whole operation (5 minutes) and the
connect phase separately (10 s). On timeout: `esp_ota_abort()`, report
`FAILED {"reason":"timeout"}`, leave the device on its current firmware. **No
cross-session resume** — a retry starts at byte 0. Record that in a comment.

**Progress.** Log every 25 %, and drive the LCD (see §6).

**Failure reasons** — short, stable strings: `no_url`, `http_status`,
`hash_mismatch`, `ota_begin_failed`, `ota_write_failed`, `image_invalid`,
`timeout`, `reboot_pending`.

**Reporting shape** (`$aws/things/<thing>/jobs/<jobId>/update`, QoS 1):

```json
{"status":"IN_PROGRESS","statusDetails":{"step":"downloading","pct":"50"}}
{"status":"SUCCEEDED","statusDetails":{"version":"2.0.4","reboot":"true"}}
{"status":"FAILED","statusDetails":{"reason":"hash_mismatch","pct":"83"}}
```

`statusDetails` is `map<string,string>` — every value must be a string.

## 6. LCD

The point of the check is that the user sees the update **before race day**, so a
queued update must be visible without a phone or a browser.

- **Prompt, idle screen:** a line such as `Firmware 2.0.4 available — long-press
  to install`. Show it only for a job seen **this session**; do not persist
  "update available" in NVS, because the release may be withdrawn while the timer
  sits in a drawer.
- **Deliberate confirmation:** two steps (long-press, then confirm) or an
  equivalent that a lap-button press during a race cannot accidentally trigger.
- **Confirmation copy must say the cost plainly:** that the current session ends
  and unsaved laps are lost.
- **During apply:** the timing display is replaced by progress (`Updating 42% — do
  not power off`). This is the only LCD work in this phase; no settings, no
  version picker.

## 7. Non-goals

- **No signature verification.** SHA-256 only. Signature checking needs the public
  key embedded and `esp_secure_boot_verify_signature_block()` in the boot path —
  Phase 3.
- No cross-session resume.
- No ring or rollout logic on the device; targeting is decided in the cloud.
- No automatic/unattended apply, ever. Removed in revision 2.
- No changes on non-touch boards; everything guarded by `BOARD_ESP32_S3_TOUCH`.

## 8. The artifact must come from this branch

A release built from `main` predates the AWS client: its image has no MQTT code at
all. Flashing it makes the device silent, permanently — it cannot report and
cannot be updated again.

The release workflow now refuses to attach the fleet marker (`ota.json`) unless
the packaged image contains the OTA job client, so a non-capable build cannot
enter fleet delivery. That guard is a safety net, not a licence to be careless:
still publish from this branch.

## 9. Acceptance criteria

1. `pio run -e esp32-s3-touch` and `pio run -e esp32-s3` both build.
2. The four heap numbers are logged at the points in §4, and on the touch board
   `internal largest` is reported before and after the fix.
3. With no job queued: nothing happens; the session behaves exactly as before.
4. Check path does not raise `-0x7F00`, and if it does, it is logged as transient,
   causes no FAILED report, and retries later.
5. With a job queued and a race running: the prompt may appear, but **no download
   starts** and timing is unaffected.
6. With a job queued and the user confirming install: serial shows `IN_PROGRESS`,
   progress lines, `SUCCEEDED`, then a reboot into the new version.
7. Tapping install mid-race is permitted, discards the session, and completes the
   update. Verify both that the laps are gone and that this was the only effect.
8. The execution is `QUEUED` before the user confirms, `IN_PROGRESS` during the
   apply, and terminal afterwards.
9. After the update, the shadow reports the new `fw_version`, and **a second power
   cycle keeps the new version** — proving the rollback confirm ran.
10. A deliberately corrupted `sha256` in the job document yields
    `FAILED {"reason":"hash_mismatch"}` with the device still running its old
    firmware.
11. `git status` shows no credential files.

## 10. Order of work

1. Heap instrumentation, as specified in §4. **Change nothing else. Report the
   numbers.**
2. The budget changes in §4 — MQTT buffers, no `s_url` static, PSRAM allocation
   for the apply buffers, transient handling of `-0x7F00`.
3. The check path, with the prompt and no apply.
4. The apply path, including the MQTT teardown and the document re-fetch.

Stop after each step and report. Step 1's numbers decide how much of step 2 is
needed; do not skip it and do not guess at the budget.

## 11. Reporting back

Report: files created/modified, the four heap numbers at each point, how the
streaming write and hash are wired, what happens on each failure path, where
`esp_ota_mark_app_valid_cancel_rollback()` is called and why there, the exact
ordering you chose for MQTT teardown versus document fetch, and which acceptance
rows you verified on hardware versus reasoned about. Flag anything in section 2
you could not satisfy.

Do not commit. Do not push. Show the diff.
