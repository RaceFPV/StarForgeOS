# StarForgeOS — STA Wi-Fi so the timer can receive firmware updates

**Audience:** the coding agent working in this repository.
**Goal:** the timer keeps the access point it has today and *additionally* joins a
user-supplied Wi-Fi network, so it can reach AWS for firmware updates — and so a
phone on that same network can still open the race UI.

Read this whole document first. Implement in the order given, then stop at the
acceptance criteria.

---

## 1. The product contract

The user is told, in the settings page, something equivalent to:

> **Wireless firmware updates** — Connect this timer to your Wi-Fi so it can
> check for firmware updates. Race timing works fine without it.

That sentence is the whole design. Three consequences:

- **Timing must never depend on Wi-Fi.** No credentials, wrong password, no
  network — the timer still boots, serves its AP, and times races exactly as it
  does today.
- **The AP stays.** `SFOS-XXXX` at `192.168.4.1` continues to exist in every
  state, so a user who never configures anything sees no change at all.
- **When a network *is* configured**, the user can reach the same UI over their
  own network at `MDNS_HOSTNAME.local` (or the DHCP address), because the timer
  is now a peer on that LAN.

## 2. What exists today (do not reinvent)

| Fact | Where |
| --- | --- |
| AP only: `WiFi.mode(WIFI_AP)`, SSID `SFOS-<mac>`, `192.168.4.1/24`, open network | `src/settings/wifi_manager.cpp` |
| No station mode anywhere in the project | `grep -rn "WIFI_STA\|WIFI_AP_STA\|WiFi.begin(" src/` is empty |
| SPIFFS mounted once, on the main thread, before the server listens | `WebServerManager::mountSpiffs()` |
| mDNS already advertises `MDNS_HOSTNAME.local` + `_http._tcp` | `web_server.cpp` ~line 208 |
| API style: `/api/snake_case`, `HTTP_GET`/`HTTP_POST` | `web_server.cpp` ~line 231 |
| AWS task starts unconditionally after AP setup and cannot connect | `StandaloneMode::begin()` → `sf_aws_start()` |

## 3. Requirements

### 3.1 Station mode, alongside the AP

- Boot into `WIFI_AP_STA`, not `WIFI_AP`.
- Start the AP exactly as today (same SSID scheme, same IP, same channel
  behaviour). No change to AP credentials or DHCP.
- If stored credentials exist, begin associating **without blocking**: no
  `while (!WiFi.status())` anywhere, no delay longer than ~50 ms in `loop()`.
- Retry policy: attempts at 5 s, 15 s, 30 s, 60 s, 120 s, then every 120 s. Do
  **not** cap the total number of retries — the timer may sit in a drawer for
  weeks and one day be plugged in near a known network. Retries must be cheap
  and silent (log at most once per state change).
- On `WL_NO_SSID_AVAIL` / `WL_CONNECT_FAILED` / `WL_CONNECTION_LOST`: return to
  the retry schedule, keep the AP up, keep timing running.

**Channel note to implement correctly:** AP and STA share one radio, so when the
STA associates, the AP moves to the STA's channel and existing AP clients are
disconnected. Connect the STA as early as possible (immediately after the AP is
up), and do not attempt the join while a race is in progress — if a race is
active when a retry is due, defer it until the race ends.

### 3.2 Storage

`Preferences` namespace `sf_wifi`:

| Key | Type | Meaning |
| --- | --- | --- |
| `ssid` | string | network name, 1–32 bytes |
| `pass` | string | passphrase, 0–63 bytes (empty = open network) |

Presence of `ssid` means "configured". The password is **write-only**: it is
never returned by any API, never rendered back into the settings field, and
never logged — not even its length beyond a byte count.

### 3.3 API

| Method | Path | Body | Response |
| --- | --- | --- | --- |
| `GET` | `/api/wifi/status` | — | JSON, see below |
| `POST` | `/api/wifi/credentials` | `{"ssid":"...","password":"..."}` | status JSON after validating and storing |
| `POST` | `/api/wifi/forget` | — | status JSON after clearing |

`GET /api/wifi/status` returns, at minimum:

```json
{
  "configured": true,
  "state": "connected",           // idle | joining | connected | failed
  "ssid": "MyNetwork",
  "ip": "192.168.1.42",           // "" unless connected
  "mdns": "sfos-a1b2.local",
  "ap_ip": "192.168.4.1",
  "last_error": ""                // e.g. "auth_failed", "no_ssid"
}
```

Validation: reject an SSID shorter than 1 or longer than 32 bytes; reject a
passphrase that is neither empty nor 8–63 bytes; reject non-printable characters.
Return `400` with a message the UI can show. On acceptance: store, set state to
`joining`, and kick the state machine — no reboot required.

### 3.4 UI

Add a settings section titled exactly **"Wireless firmware updates"** with the
explanatory line from §1. Contents:

- **Network name** — text input, prefilled with the stored SSID (SSID is not
  secret).
- **Password** — `type="password"`, always empty on render, placeholder
  "unchanged" when credentials already exist. A separate "Forget network" button
  clears everything, because an empty password field must not be ambiguous
  between "unchanged" and "open network".
- **Save** — posts to `/api/wifi/credentials`, shows the returned state.
- **Status line** — rendered from `/api/wifi/status`, polled every ~5 s while the
  settings panel is open:
  - not configured → "Not configured — updates over Wi-Fi are off"
  - joining → "Connecting to <ssid>…"
  - connected → "Connected to <ssid> — <ip>"
  - failed → "Could not connect to <ssid> (<last_error>) — timing is unaffected"

Do not restyle the page. Match the existing markup and CSS conventions.

### 3.5 Gate the AWS task on connectivity

`sf_aws_start()` must be called **only** when the state machine first reaches
`connected`. Today it fires straight after AP setup and burns all eight connect
attempts resolving a hostname that cannot resolve.

- No credentials → never start, log once ("AWS disabled: no Wi-Fi network
  configured").
- STA drops later → do not restart the AWS task. Its attempts are already
  bounded, and the session is short by design. Log the reason.

### 3.6 mDNS on the station interface

`MDNS_HOSTNAME.local` must resolve from the user's LAN once STA is connected. If
the responder was started while AP-only and does not answer on the STA address,
stop and re-start it after the first successful association (`MDNS.end()` →
`MDNS.begin()` → re-add the `_http._tcp` service). Verify on a phone or laptop on
the same network.

## 4. Non-goals

- No OTA download, no job handling, no UI for update availability. (Phase 2.)
- No captive-portal / auto-detect flow.
- No change to AP SSID scheme, AP IP, or DHCP range.
- No changes on non-touch boards: **every change here is guarded** by
  `BOARD_ESP32_S3_TOUCH`, and `pio run -e esp32-s3` must still build.

## 5. Security requirements

- The password is never logged, never echoed by the API, never prefilled.
- The credentials endpoint accepts `POST` only.
- **Known residual risk:** the AP is currently open (`WIFI_AP_PASSWORD ""`), so
  anyone in radio range can reach the settings page and overwrite these
  credentials. Do not silently "fix" this in this change — note it prominently in
  the code comment and in your report, because closing it (per-device AP password
  shown on the LCD) is a separate product decision.

## 6. Behaviour matrix (must all hold)

| Situation | Expected |
| --- | --- |
| No credentials stored | AP up, timer times races, AWS never starts, status "Not configured" |
| Valid credentials, network present | AP up, STA connected, mDNS resolves on the LAN, AWS task starts, one shadow report |
| Wrong password | AP up, state `failed` with `auth_failed`, retries per schedule, timing unaffected, AWS never starts |
| Network absent (drawer / venue) | AP up, silent retries every 2 min, timing unaffected |
| Credentials changed while running | old association dropped, new one attempted without reboot |
| Forget network | NVS cleared, STA disconnected, AP remains, status back to "Not configured" |
| STA drops mid-session | timer keeps working; AWS task is not restarted |

## 7. Acceptance criteria

1. `pio run -e esp32-s3-touch` and `pio run -e esp32-s3` both build.
2. With no credentials: serial shows the AP start line and one
   "AWS disabled: no Wi-Fi network configured" line; the UI shows "Not
   configured"; a race still starts and stops normally.
3. Save valid credentials from the UI: serial shows one association attempt and
   one success line; `GET /api/wifi/status` reports `connected` with an IP; a
   phone on the same network loads the UI at `MDNS_HOSTNAME.local`.
4. Save a wrong password: state `failed`/`auth_failed`, no reboot, no blocking,
   race timing still works.
5. `POST /api/wifi/forget` returns the timer to AP-only behaviour without a
   reboot.
6. With credentials valid and the network present, the previously added AWS path
   connects and publishes (the shadow query succeeds).
7. `git status` shows no credential files staged; `data/aws/` still ignored.

## 8. Files (suggested, not prescriptive)

Create `src/net/wifi_sta.h` / `.cpp` (state machine, NVS, retries) and add the
three API handlers to the existing `WebServerManager`. Modify
`wifi_manager.cpp` to use `WIFI_AP_STA`, `standalone_mode.cpp` to start the state
machine and gate `sf_aws_start()`, and the settings page markup/JS.

## 9. Reporting back

Report: files created/modified, how the state machine is driven (task vs polled —
say which and why), what the UI copy says, any deviation from this spec and why,
and the behaviour-matrix rows you actually verified versus reasoned about. Note
the open-AP risk explicitly.

Do not commit. Do not push. Show the diff.
