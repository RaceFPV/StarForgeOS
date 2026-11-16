# StarforgeOS — AWS IoT Phase 1 implementation spec

**Audience:** the coding agent working in this repository.
**Goal:** one lap timer connects to AWS IoT Core over mutual TLS, publishes a
telemetry record to its Device Shadow, and nothing else in the firmware changes
behaviour.

Read this whole document before writing code. Then implement it in the order
given and stop after the acceptance tests — do not proceed to Phase 2 (OTA
jobs, downloads) even though some hooks for it are described.

---

## 1. Context you need

This firmware runs on **RaceFPV race lap timers**: battery/DC powered, kept in a
drawer for days or weeks, powered on shortly before a race, used to time a race,
then hard-powered-off. Devices are therefore **online for minutes every few
weeks**. This single fact drives every design decision below:

- The cloud can never "ask" the device anything. The device reports what
  happened since it last checked in.
- Telemetry is a **store-and-forward log**, not a live state snapshot.
- The device has **no reliable wall clock** at boot (no RTC, no guaranteed NTP
  before the first publish). It must never fabricate timestamps; the cloud
  stamps arrival time.
- Connection attempts must never block the UI, the timing loop, or the existing
  web server. A missed connection is normal, not an error.

## 2. Target environment

- PlatformIO, **Arduino framework** (`framework = arduino`), ESP32-S3.
- Target env: **`esp32-s3-touch`** (Waveshare ESP32-S3-Touch-LCD-2 / 2.1).
  - ST7789 240x320 display, CST816D capacitive touch, 8 MB PSRAM.
  - LVGL 8 UI, TFT_eSPI, ESPAsyncWebServer, FastLED, ArduinoJson 6.
  - Filesystem: **SPIFFS** (`board_build.filesystem = spiffs`, `data/`).
- The platform package is `pioarduino/platform-espressif32` (stable), which
  ships **ESP-IDF 5.x**. Confirm the actual IDF version from the build output
  before choosing API shapes — see §7.
- Other environments in `platformio.ini` (`esp32-c3-*`, `esp32dev`, `jc2432w328c`,
  test envs) **must continue to build**. Everything you add must be compiled out
  when the board is not an S3 target — guard with `#if defined(BOARD_ESP32_S3_TOUCH)`
  or an equivalent, and never add a dependency that breaks other envs.

## 3. What already exists in AWS (do not create any of it)

Provisioned by OpenTofu in a separate repo (`starforge-ota`), already applied:

| Thing | Value |
| --- | --- |
| Thing name | `racefpv-vtx-001` (also the **required MQTT client id**) |
| Thing type | `starforge_device` |
| Thing groups | `starforge-ring-canary`, `starforge-hw-esp32s3` |
| Policy | `starforge-device-policy`, attached to the device certificate |
| Endpoint | resolve with `aws iot describe-endpoint --endpoint-type iot:Data-ATS` |
| Region | `us-east-1` |

The policy grants, for this device only:

| Action | Topic |
| --- | --- |
| `iot:Connect` | `client/${iot:Connection.Thing.ThingName}` — **client id must equal the thing name** |
| `iot:Publish` | `$aws/things/<thing>/shadow/*` and `$aws/things/<thing>/jobs/*` |
| `iot:Subscribe` / `iot:Receive` | `$aws/things/<thing>/shadow/*` and `$aws/things/<thing>/jobs/*` |

Consequences:

- Connecting with any client id other than the thing name is **denied**.
- Publishing to anything outside those topic trees is **denied**. MQTT denials
  are silent at the broker — the publish simply does not arrive. If telemetry
  reaches nothing, check the topic string before checking the code.

## 4. Credentials and configuration on the device

The device must load its identity from SPIFFS at runtime. **Nothing may be
compiled in, and no key or certificate may be committed to git.**

Files, provisioned into the SPIFFS image (see §9 for the script that builds them):

| Path | Contents |
| --- | --- |
| `/aws/cert.pem` | device certificate (PEM) |
| `/aws/device.key` | device private key (PEM, SEC1 or PKCS#8) |
| `/aws/root-CA.crt` | Amazon Root CA 1 (PEM) |
| `/aws/endpoint.txt` | `xxxxx-ats.iot.us-east-1.amazonaws.com` |
| `/aws/thing.txt` | `racefpv-vtx-001` |

Requirements:

- Strip trailing whitespace/newlines when reading `endpoint.txt` and `thing.txt`.
- Read PEM files into memory and **keep the buffers alive for the lifetime of the
  MQTT client** (allocate from PSRAM/heap once; do not free while connected).
- **Never log key or certificate contents.** Logging "credential loaded, N bytes"
  is fine.
- If any file is missing: log once, do not connect, do not crash, leave the rest
  of the firmware completely unaffected. A lap timer without credentials is a
  perfectly functional lap timer.

## 5. MQTT client

Use the ESP-IDF MQTT client (`esp_mqtt_client.h`), not PubSubClient — it handles
mutual TLS, keepalive, LWT and QoS 1 correctly, and Phase 2 (Jobs) builds on it.

Configuration essentials:

```c
esp_mqtt_client_config_t cfg = {0};
cfg.broker.address.uri                    = "mqtts://<endpoint>:8883";
cfg.credentials.client_id                 = thing_name;      // MUST equal thing name
cfg.credentials.authentication.certificate = cert_pem;
cfg.credentials.authentication.key         = key_pem;
cfg.broker.verification.certificate        = ca_pem;         // Amazon Root CA 1
cfg.session.keepalive                      = 60;
cfg.session.last_will.topic                = "$aws/things/<thing>/shadow/update";
cfg.session.last_will.msg                  = will_json;      // see below
cfg.session.last_will.qos                  = 1;
cfg.session.last_will.retain               = 0;
cfg.network.reconnect_timeout_ms           = 5000;
cfg.network.timeout_ms                     = 10000;
```

- Run the client in its **own FreeRTOS task** (e.g. `xTaskCreatePinnedToCore`,
  priority 5, stack 8192, core 0). The Arduino `loop()` runs on core 1 and must
  not be blocked.
- **Retry with bounded backoff**: attempt at boot, then 5s, 10s, 20s, 40s, 60s,
  60s… Give up after ~8 attempts (roughly 5 minutes). A device that cannot
  connect should go quiet, not hammer the network or the battery. Log each
  attempt at most once.
- Do not log at ERROR level for a failed connection attempt; it is expected.

## 6. Telemetry: what to publish

Publish to `$aws/things/<thing>/shadow/update`, QoS 1:

```json
{"state":{"reported":{
  "fw_version":"1.0.0",
  "uptime_s":42,
  "heap_low":184320,
  "resets":{"poweron":3,"panic":0,"wdt":1,"brownout":0},
  "usage":{"races":12,"laps":340},
  "last_result":"BOOT_OK"
}}}
```

Rules:

- **No `ts` field.** The device has no trustworthy clock; the cloud records
  arrival time. Do not send `millis()` or a fabricated epoch.
- `fw_version`: the firmware version string. Prefer a single source of truth —
  read from the app descriptor (`esp_app_get_description()->version`) and set
  that at build time, or define one constant used by both this payload and any
  existing UI version display. Do not duplicate the string in two places.
- `uptime_s`: seconds since this boot.
- `heap_low`: `esp_get_minimum_free_heap_size()` at publish time (low-water mark
  since boot).
- `resets`: **cumulative counters since the last acknowledged report**, persisted
  in NVS. See §8.
- `usage`: cumulative race/lap counters since the last acknowledged report,
  persisted in NVS.
- `last_result`: short string describing the previous session's outcome
  (`BOOT_OK`, `OTA_OK`, `OTA_FAIL:<reason>`, `PANIC`) — a placeholder enum now,
  filled in by Phase 2.
- Publish **once per session** shortly after connect, then at most every 5
  minutes while still connected. Nothing faster: airtime and battery matter more
  than resolution on a device that is online for minutes.
- Last Will: `{"state":{"reported":{"online":false}}}` with QoS 1, not retained.
  After a successful connect, publish `{"state":{"reported":{"online":true,"fw_version":"…"}}}`
  is optional — keep Phase 1 minimal and skip it if it complicates the flow.

## 7. ESP-IDF version check (do this before coding)

`esp_mqtt_client_config_t` changed shape between IDF 4.x (flat fields: `uri`,
`client_id`, `cert_pem`…) and IDF 5.x (nested: `broker.address.uri`,
`credentials.client_id`, `credentials.authentication.certificate`…). This repo
builds against IDF 5.x, so use the nested form above.

If the compiler rejects a field: **read the `esp_mqtt_client.h` that ships with
the pinned platform package** (`~/.platformio/packages/framework-espidf/components/mqtt/esp-mqtt/include/esp_mqtt_client.h`)
and match the real struct. Do not guess, and do not paste code from a blog post.

## 8. Persistence: counters that survive a hard power-off

Use the Arduino `Preferences` library (NVS wrapper), namespace `sf_iot`.

Counter semantics — this is the part that matters:

1. **On boot**: read `esp_reset_reason()` and increment the matching bucket
   (`ESP_RST_POWERON`→poweron, `ESP_RST_PANIC`→panic, `ESP_RST_INT_WDT`/
   `ESP_RST_TASK_WDT`/`ESP_RST_WDT`→wdt, `ESP_RST_BROWNOUT`→brownout,
   anything else→poweron is wrong, count as `other` if you add a bucket; do not
   silently fold unknowns into poweron). Persist immediately.
2. **Session end is a hard power-off.** Anything incremented must be written to
   NVS at the moment it happens, not held in RAM until shutdown — there is no
   shutdown. Races/laps counters: write on each increment (cheap) or on each race
   completion (cheaper); choose the latter and note it.
3. **Only clear counters after the publish is acknowledged.** With QoS 1, that is
   `MQTT_EVENT_PUBLISHED` for the matching `msg_id`. Write zeros then. If the
   device loses power before the PUBACK, the next session reports the same
   numbers again — duplicates are correct, silent data loss is not.
4. Cap any single counter at a sane maximum (e.g. 10000) so a corrupted NVS value
   cannot produce absurd reports.

## 9. Build-time provisioning script (this repo)

Add `scripts/provision-aws-fs.sh` (executable). It assembles the SPIFFS image
contents from the OpenTofu-generated credentials and flashes the filesystem:

```bash
#!/usr/bin/env bash
# Provision AWS IoT credentials into the SPIFFS image, then flash it.
set -euo pipefail
OTA_REPO="${STARFORGE_OTA_REPO:-$HOME/git/starforge-ota}"
THING="${1:?usage: provision-aws-fs.sh <thing-name>}"
ENV="${PIOENV:-esp32-s3-touch}"
SRC="$OTA_REPO/infra/secrets/$THING"

[ -f "$SRC/cert.pem" ]   || { echo "missing $SRC/cert.pem (run tofu apply in starforge-ota)" >&2; exit 1; }
[ -f "$SRC/device.key" ] || { echo "missing $SRC/device.key" >&2; exit 1; }

EP=$(aws iot describe-endpoint --endpoint-type iot:Data-ATS --query endpointAddress --output text)

mkdir -p data/aws
cp "$SRC/cert.pem"        data/aws/cert.pem
cp "$SRC/device.key"      data/aws/device.key
chmod 600                 data/aws/device.key
printf '%s\n' "$EP"    >  data/aws/endpoint.txt
printf '%s\n' "$THING" >  data/aws/thing.txt

# Amazon Root CA 1, pinned by URL; verify the SHA-256 if you cache it.
curl -fsSL https://www.amazontrust.com/repository/AmazonRootCA1.pem -o data/aws/root-CA.crt

pio run -e "$ENV" -t uploadfs
```

And add to `.gitignore`:

```
data/aws/
```

Verify with `git status --porcelain` that no credential file ever shows up as
untracked-modifiable. If it does, fix `.gitignore` before doing anything else.

## 10. Files and responsibilities

Create (all new, additive):

| File | Responsibility |
| --- | --- |
| `src/aws/aws_iot.h` / `.cpp` | Client lifecycle: credential load, config, task, publish, event handling |
| `src/aws/telemetry.h` / `.cpp` | NVS counters, reset-reason mapping, heap low-water, payload assembly (ArduinoJson) |
| `src/aws/fs_creds.h` / `.cpp` | SPIFFS reads, trailing-whitespace trimming, buffer ownership |
| `scripts/provision-aws-fs.sh` | §9 |

Modify, minimally:

| File | Change |
| --- | --- |
| `src/main.cpp` (or wherever WiFi connection is established) | Hook: once WiFi is connected, start the AWS task. Must not block `setup()`/`loop()`. Find the existing WiFi state machine — do not add a second one. |
| Race/timing code (wherever a race finishes) | Call `sf_usage_race_completed(lap_count)` to increment usage counters. One call site, no restructuring. |

Rules:

- Guard all of it for the S3-touch build so other envs are untouched.
- Do not touch LVGL/UI, web server, FastLED, ADC, or the existing timing logic
  beyond the single usage-counter call.
- Do not add libraries to `platformio.ini` unless genuinely required; if you must,
  add them to the `esp32-s3-touch` env only, never to `[env]`.
- Keep formatting consistent with the existing files.

## 11. Non-goals for this task (explicitly out of scope)

- No OTA download, no job subscription, no `esp_https_ota`. (Phase 2.)
- No UI for update status. (Phase 2 — but keep the MQTT work off the LVGL thread
  so it can be added without rework.)
- No NTP/time sync.
- No change to WiFi credentials handling.
- No changes in the `starforge-ota` repo. Ever, from here.

## 12. Acceptance criteria

Build:

- `pio run -e esp32-s3-touch` succeeds with no new warnings.
- Every other env in `platformio.ini` still compiles (`pio run -e esp32-s3` at
  minimum, to prove the guards work).

Behaviour, with credentials present:

1. Serial shows one line: credentials loaded (byte counts, no contents).
2. Serial shows: `MQTT connected` with the client id equal to the thing name.
3. Serial shows: shadow publish sent (msg id), then `PUBACK`, then counters cleared.
4. On AWS, the shadow contains the fields from §6:
   `aws iot-data get-thing-shadow --thing-name racefpv-vtx-001`
5. Power-cycle 3 times: `resets.poweron` increments by 3 and the reported value
   tracks the number of boots since the last acknowledged publish.
6. Disconnect WiFi, boot: no crash, no UI stall, retries bounded, then quiet.
   Existing lap timing still works normally.

Behaviour, without credentials (`data/aws` absent):

7. Boot logs one warning, device behaves exactly as before, no error dialogs, no
   watchdog reboots.

Regression:

8. Web interface still serves, LVGL UI still responsive, ADC/timing unaffected.

## 13. Reporting back

When done, report: files created/modified, the exact IDF struct shape you found
in `esp_mqtt_client.h`, any deviation from this spec and why, and the serial log
from a successful connect (redact the certificate/key if it ever appears — it
should not).

Do not commit. Do not push. Show the diff.
