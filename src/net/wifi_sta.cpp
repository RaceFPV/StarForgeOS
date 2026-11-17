#include "wifi_sta.h"

#if defined(BOARD_ESP32_S3_TOUCH)

#include "config/config.h"
#include "aws/aws_iot.h"

#include <WiFi.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <esp_wifi.h>

#include <atomic>
#include <cstdio>
#include <cstring>

// Polled from StandaloneMode::process(). No waits and no delay() here:
// association, backoff, and the race deferral are all decided by millis().

namespace {

enum State : uint8_t {
    ST_IDLE = 0,
    ST_JOINING,
    ST_CONNECTED,
    ST_FAILED
};

struct Snap {
    bool configured;
    State state;
    char ssid[33];
    char last_error[24];
    char ip[16];
};

portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
Snap s_snap = {};
char s_pass[64] = {};

std::atomic<bool> s_kick{false};
std::atomic<bool> s_forget{false};
std::atomic<int> s_disc_reason{-1};
std::atomic<uint32_t> s_disc_seq{0};
std::atomic<uint32_t> s_got_ip_seq{0};
std::atomic<bool> s_suppress_disc{false};

bool s_began = false;
bool s_begin_pending = false;
bool s_deferred = false;
bool s_logged_joining = false;
bool s_mdns_on_sta = false;
bool s_aws_started = false;
uint32_t s_failures = 0;
uint32_t s_next_ms = 0;
uint32_t s_join_started = 0;
uint32_t s_disc_seen = 0;
uint32_t s_ip_mark = 0;
char s_logged_error[24] = {};
SfWifiFirstConnectFn s_on_first = nullptr;

const char *state_name(State state) {
    switch (state) {
        case ST_JOINING: return "joining";
        case ST_CONNECTED: return "connected";
        case ST_FAILED: return "failed";
        case ST_IDLE:
        default: return "idle";
    }
}

bool printable(const char *s) {
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(s); *p; ++p) {
        if (*p < 0x20 || *p > 0x7e) {
            return false;
        }
    }
    return true;
}

void set_err(char *err, size_t err_len, const char *msg) {
    if (err && err_len) {
        snprintf(err, err_len, "%s", msg);
    }
}

Snap load_snap(void) {
    Snap copy;
    portENTER_CRITICAL(&s_mux);
    copy = s_snap;
    portEXIT_CRITICAL(&s_mux);
    return copy;
}

void store_state(State state, const char *err, const char *ip) {
    portENTER_CRITICAL(&s_mux);
    s_snap.state = state;
    if (err) {
        snprintf(s_snap.last_error, sizeof(s_snap.last_error), "%s", err);
    }
    if (ip) {
        snprintf(s_snap.ip, sizeof(s_snap.ip), "%s", ip);
    }
    if (state != ST_CONNECTED) {
        s_snap.ip[0] = '\0';
    }
    portEXIT_CRITICAL(&s_mux);
}

void copy_secrets(char *ssid, size_t ssid_len, char *pass, size_t pass_len) {
    portENTER_CRITICAL(&s_mux);
    snprintf(ssid, ssid_len, "%s", s_snap.ssid);
    snprintf(pass, pass_len, "%s", s_pass);
    portEXIT_CRITICAL(&s_mux);
}

uint32_t backoff_ms(uint32_t failures) {
    static const uint32_t kDelays[] = {5000, 15000, 30000, 60000, 120000};
    if (failures == 0) {
        return 0;
    }
    uint32_t index = failures - 1;
    if (index > 4) {
        index = 4;
    }
    return kDelays[index];
}

const char *reason_to_error(int reason) {
    switch (reason) {
        case WIFI_REASON_AUTH_EXPIRE:
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_MIC_FAILURE:
        case WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT:
        case WIFI_REASON_802_1X_AUTH_FAILED:
        case WIFI_REASON_AUTH_FAIL:
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
            return "auth_failed";
        case WIFI_REASON_NO_AP_FOUND:
        case WIFI_REASON_BEACON_TIMEOUT:
            return "no_ssid";
        default:
            return "connection_lost";
    }
}

bool local_reason(int reason) {
    return reason == WIFI_REASON_UNSPECIFIED || reason == WIFI_REASON_ASSOC_LEAVE;
}

// Our own WiFi.disconnect() also produces a STA disconnect event. Swallow
// that one. An auth failure that arrives in the same window is still reported.
const char *take_disconnect(void) {
    uint32_t seq = s_disc_seq.load();
    if (seq == s_disc_seen) {
        return nullptr;
    }
    int reason = s_disc_reason.load();
    s_disc_seen = seq;
    bool suppressed = s_suppress_disc.exchange(false);
    if (suppressed && local_reason(reason)) {
        return nullptr;
    }
    return reason_to_error(reason);
}

const char *status_error(wl_status_t status) {
    if (status == WL_NO_SSID_AVAIL) {
        return "no_ssid";
    }
    if (status == WL_CONNECT_FAILED) {
        return "auth_failed";
    }
    if (status == WL_CONNECTION_LOST) {
        return "connection_lost";
    }
    return nullptr;
}

void local_disconnect(void) {
    // Timeout 0: the default waits 100 ms, and WiFi.begin() waits up to a
    // second if it still sees the old association. Drop first, begin later.
    s_suppress_disc.store(true);
    WiFi.disconnect(false, false, 0);
}

void enter_failed(const char *err, bool from_connected) {
    store_state(ST_FAILED, err, nullptr);
    if (from_connected && s_aws_started) {
        Serial.printf("STA dropped (%s); AWS task not restarted\n", err);
    } else if (strcmp(s_logged_error, err) != 0) {
        Serial.printf("STA failed (%s)\n", err);
        snprintf(s_logged_error, sizeof(s_logged_error), "%s", err);
    }
    if (s_failures < UINT32_MAX) {
        s_failures++;
    }
    s_next_ms = millis() + backoff_ms(s_failures);
    s_deferred = false;
    local_disconnect();
}

void on_connected(void) {
    char ip[16];
    snprintf(ip, sizeof(ip), "%s", WiFi.localIP().toString().c_str());
    store_state(ST_CONNECTED, "", ip);
    s_failures = 0;
    s_deferred = false;
    s_logged_joining = false;
    s_logged_error[0] = '\0';
    Serial.printf("STA connected, ip %s\n", ip);
#if defined(SF_DEBUG_HEAP)
    sf_aws_log_heap("wifi connected");
#endif

    if (!s_mdns_on_sta) {
        MDNS.end();
        if (MDNS.begin(MDNS_HOSTNAME)) {
            MDNS.addService("http", "tcp", WEB_SERVER_PORT);
            s_mdns_on_sta = true;
            Serial.printf("mDNS restarted: %s.local\n", MDNS_HOSTNAME);
        }
    }

    if (!s_aws_started) {
        s_aws_started = true;
        if (s_on_first) {
            s_on_first();
        }
    }
}

void issue_begin(void) {
    char ssid[33];
    char pass[64];
    copy_secrets(ssid, sizeof(ssid), pass, sizeof(pass));
    if (ssid[0] == '\0') {
        memset(pass, 0, sizeof(pass));
        s_begin_pending = false;
        return;
    }
    s_ip_mark = s_got_ip_seq.load();
    WiFi.begin(ssid, pass);
    memset(pass, 0, sizeof(pass));
    s_join_started = millis();
    s_begin_pending = false;
}

void start_join(void) {
    Snap snap = load_snap();
    if (snap.ssid[0] == '\0') {
        return;
    }
    store_state(ST_JOINING, "", nullptr);
    s_join_started = millis();
    if (!s_logged_joining) {
        Serial.printf("STA joining %s\n", snap.ssid);
        s_logged_joining = true;
    }
    if (WiFi.status() == WL_CONNECTED) {
        s_begin_pending = true;
        local_disconnect();
        return;
    }
    issue_begin();
}

void on_wifi_event(arduino_event_id_t event, arduino_event_info_t info) {
    if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
        s_disc_reason.store(static_cast<int>(info.wifi_sta_disconnected.reason));
        s_disc_seq.fetch_add(1);
    } else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
        s_got_ip_seq.fetch_add(1);
    }
}

}  // namespace

void sf_wifi_sta_on_first_connect(SfWifiFirstConnectFn fn) {
    s_on_first = fn;
}

void sf_wifi_sta_begin(void) {
    if (s_began) {
        return;
    }

    memset(&s_snap, 0, sizeof(s_snap));
    s_snap.state = ST_IDLE;
    s_pass[0] = '\0';

    Preferences prefs;
    if (prefs.begin("sf_wifi", true)) {
        prefs.getString("ssid", s_snap.ssid, sizeof(s_snap.ssid));
        prefs.getString("pass", s_pass, sizeof(s_pass));
        prefs.end();
    }

    // The stack must not reconnect on its own. That would join during a race
    // and ignore the backoff schedule below.
    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);
    WiFi.onEvent(on_wifi_event);

    if (s_snap.ssid[0] == '\0') {
        s_snap.configured = false;
        s_pass[0] = '\0';
        Serial.println("AWS disabled: no Wi-Fi network configured");
    } else {
        s_snap.configured = true;
        s_snap.state = ST_JOINING;
        s_kick.store(true);
    }
    s_began = true;
}

void sf_wifi_sta_poll(bool race_active) {
    if (!s_began) {
        return;
    }

    bool forget = s_forget.exchange(false);
    bool kick = s_kick.exchange(false);
    if (forget) {
        local_disconnect();
        s_deferred = false;
        s_begin_pending = false;
        s_failures = 0;
        s_logged_joining = false;
        return;
    }

    Snap snap = load_snap();
    if (kick) {
        s_failures = 0;
        s_logged_joining = false;
        s_logged_error[0] = '\0';
        s_deferred = false;
        if (!snap.configured) {
            local_disconnect();
            return;
        }
        if (race_active) {
            local_disconnect();
            s_deferred = true;
            store_state(ST_JOINING, "", nullptr);
            return;
        }
        start_join();
        return;
    }

    if (!snap.configured) {
        return;
    }

    if (s_begin_pending) {
        if (WiFi.status() == WL_CONNECTED) {
            if (static_cast<int32_t>(millis() - s_join_started) > 10000) {
                s_begin_pending = false;
                enter_failed("connection_lost", false);
            }
            return;
        }
        s_disc_seen = s_disc_seq.load();
        s_suppress_disc.store(false);
        issue_begin();
        return;
    }

    wl_status_t status = WiFi.status();
    // A new GOT_IP after issue_begin(). An older association still reports
    // WL_CONNECTED until the disconnect event lands, so status alone is not enough.
    if (snap.state == ST_JOINING &&
        status == WL_CONNECTED &&
        s_got_ip_seq.load() != s_ip_mark) {
        on_connected();
        return;
    }
    if (snap.state == ST_CONNECTED && status == WL_CONNECTED) {
        return;
    }
    if (snap.state == ST_CONNECTED) {
        const char *err = take_disconnect();
        if (!err) {
            err = status_error(status);
        }
        if (!err) {
            err = "connection_lost";
        }
        enter_failed(err, true);
        return;
    }

    if (race_active) {
        if (snap.state == ST_JOINING && !s_deferred) {
            local_disconnect();
            s_deferred = true;
        }
        return;
    }

    if (s_deferred) {
        s_deferred = false;
        start_join();
        return;
    }

    if (snap.state == ST_JOINING) {
        const char *err = take_disconnect();
        if (!err) {
            err = status_error(status);
        }
        if (err) {
            enter_failed(err, false);
            return;
        }
        // The Arduino stack often stays in WL_DISCONNECTED with no SSID
        // instead of raising WL_NO_SSID_AVAIL. Give the attempt a window,
        // then use the same retry schedule as an explicit no_ssid.
        if (static_cast<int32_t>(millis() - s_join_started) >= 20000) {
            enter_failed("no_ssid", false);
        }
        return;
    }

    if (snap.state == ST_FAILED && static_cast<int32_t>(millis() - s_next_ms) >= 0) {
        start_join();
    }
}

bool sf_wifi_sta_set_credentials(const char *ssid, const char *password,
                                 bool have_password, char *err, size_t err_len) {
    if (!ssid) {
        ssid = "";
    }
    if (!password) {
        password = "";
    }

    size_t ssid_len = strlen(ssid);
    if (ssid_len < 1 || ssid_len > 32) {
        set_err(err, err_len, "SSID must be 1 to 32 characters");
        return false;
    }
    if (!printable(ssid)) {
        set_err(err, err_len, "Only printable characters are allowed");
        return false;
    }
    if (have_password) {
        size_t pass_len = strlen(password);
        if (!(pass_len == 0 || (pass_len >= 8 && pass_len <= 63))) {
            set_err(err, err_len, "Password must be empty or 8 to 63 characters");
            return false;
        }
        if (!printable(password)) {
            set_err(err, err_len, "Only printable characters are allowed");
            return false;
        }
    }

    Snap snap = load_snap();
    bool keep_pass = !have_password && snap.configured;

    Preferences prefs;
    if (!prefs.begin("sf_wifi", false)) {
        set_err(err, err_len, "Could not store Wi-Fi settings");
        return false;
    }
    if (prefs.putString("ssid", ssid) == 0) {
        prefs.end();
        set_err(err, err_len, "Could not store Wi-Fi settings");
        return false;
    }
    if (!keep_pass) {
        // putString returns strlen, so an open network (empty passphrase) reports 0.
        size_t wrote = prefs.putString("pass", password);
        bool stored = (password[0] == '\0') ? prefs.isKey("pass") : (wrote == strlen(password));
        if (!stored) {
            prefs.end();
            set_err(err, err_len, "Could not store Wi-Fi settings");
            return false;
        }
    }
    prefs.end();

    portENTER_CRITICAL(&s_mux);
    snprintf(s_snap.ssid, sizeof(s_snap.ssid), "%s", ssid);
    if (!keep_pass) {
        snprintf(s_pass, sizeof(s_pass), "%s", password);
    }
    s_snap.configured = true;
    s_snap.state = ST_JOINING;
    s_snap.last_error[0] = '\0';
    s_snap.ip[0] = '\0';
    portEXIT_CRITICAL(&s_mux);
    s_kick.store(true);
    return true;
}

void sf_wifi_sta_forget(void) {
    Preferences prefs;
    if (prefs.begin("sf_wifi", false)) {
        prefs.remove("ssid");
        prefs.remove("pass");
        prefs.end();
    }

    portENTER_CRITICAL(&s_mux);
    memset(s_pass, 0, sizeof(s_pass));
    memset(s_snap.ssid, 0, sizeof(s_snap.ssid));
    memset(s_snap.last_error, 0, sizeof(s_snap.last_error));
    s_snap.ip[0] = '\0';
    s_snap.configured = false;
    s_snap.state = ST_IDLE;
    portEXIT_CRITICAL(&s_mux);
    s_forget.store(true);
}

size_t sf_wifi_sta_status_json(char *buf, size_t len) {
    if (!buf || len == 0) {
        return 0;
    }
    Snap snap = load_snap();
    char host[32];
    char ap[16];
    snprintf(host, sizeof(host), "%s.local", MDNS_HOSTNAME);
    snprintf(ap, sizeof(ap), "%s", WiFi.softAPIP().toString().c_str());

    StaticJsonDocument<384> doc;
    doc["configured"] = snap.configured;
    doc["state"] = state_name(snap.state);
    doc["ssid"] = snap.ssid;
    doc["ip"] = (snap.state == ST_CONNECTED) ? snap.ip : "";
    doc["mdns"] = host;
    doc["ap_ip"] = ap;
    doc["last_error"] = snap.last_error;
    size_t n = serializeJson(doc, buf, len);
    if (n == 0 || n >= len) {
        buf[0] = '\0';
        return 0;
    }
    return n;
}

#endif
