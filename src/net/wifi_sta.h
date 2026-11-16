#pragma once

#include <stddef.h>
#include <stdbool.h>

// Called once, on the first transition into the connected state.
typedef void (*SfWifiFirstConnectFn)(void);

#if defined(BOARD_ESP32_S3_TOUCH)

// Load sf_wifi from NVS and arm the polled state machine. Does not block
// on association and does not call sf_aws_start().
void sf_wifi_sta_begin(void);

// One non-blocking step. Call from the main loop. When race_active is true,
// a due join is deferred until a later poll sees the race end.
void sf_wifi_sta_poll(bool race_active);

void sf_wifi_sta_on_first_connect(SfWifiFirstConnectFn fn);

// have_password false keeps the stored passphrase when one exists, so a blank
// settings field can mean "unchanged". An explicit empty password is an open
// network. err receives a message the settings page can show.
bool sf_wifi_sta_set_credentials(const char *ssid, const char *password,
                                 bool have_password, char *err, size_t err_len);

void sf_wifi_sta_forget(void);

// Status object from section 3.3. Never includes the password.
// Returns bytes written, or 0 if buf is too small.
size_t sf_wifi_sta_status_json(char *buf, size_t len);

#else

inline void sf_wifi_sta_begin(void) {}
inline void sf_wifi_sta_poll(bool) {}
inline void sf_wifi_sta_on_first_connect(SfWifiFirstConnectFn) {}
inline bool sf_wifi_sta_set_credentials(const char *, const char *, bool, char *err, size_t err_len) {
    if (err && err_len) {
        err[0] = '\0';
    }
    return false;
}
inline void sf_wifi_sta_forget(void) {}
inline size_t sf_wifi_sta_status_json(char *buf, size_t len) {
    if (buf && len) {
        buf[0] = '\0';
    }
    return 0;
}

#endif
