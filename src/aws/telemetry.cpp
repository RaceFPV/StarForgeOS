#include "telemetry.h"

#if defined(BOARD_ESP32_S3_TOUCH)

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <atomic>
#include "config/config.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const uint32_t kCounterCap = 10000;
static const char *kNs = "sf_iot";

static portMUX_TYPE s_init_mux = portMUX_INITIALIZER_UNLOCKED;
static StaticSemaphore_t s_lock_buf;
static SemaphoreHandle_t s_lock = nullptr;

static SemaphoreHandle_t counter_lock() {
    if (s_lock) {
        return s_lock;
    }
    portENTER_CRITICAL(&s_init_mux);
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutexStatic(&s_lock_buf);
    }
    portEXIT_CRITICAL(&s_init_mux);
    return s_lock;
}

static uint32_t clamp_counter(uint32_t value) {
    return value > kCounterCap ? kCounterCap : value;
}

static void lock_counters() {
    xSemaphoreTake(counter_lock(), portMAX_DELAY);
}

static void unlock_counters() {
    xSemaphoreGive(s_lock);
}

static bool open_prefs(Preferences *prefs) {
    return prefs->begin(kNs, false);
}

void sf_telemetry_boot(void) {
    static bool counted = false;
    if (counted) {
        return;
    }
    counted = true;

    const char *key = "other";
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:
            key = "poweron";
            break;
        case ESP_RST_PANIC:
            key = "panic";
            break;
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT:
            key = "wdt";
            break;
        case ESP_RST_BROWNOUT:
            key = "brown";
            break;
        default:
            key = "other";
            break;
    }

    lock_counters();
    Preferences prefs;
    if (open_prefs(&prefs)) {
        uint32_t value = clamp_counter(prefs.getUInt(key, 0));
        if (value < kCounterCap) {
            value++;
        }
        prefs.putUInt(key, value);
        prefs.end();
    }
    unlock_counters();
}

void sf_usage_race_completed(uint32_t lap_count) {
    if (lap_count > kCounterCap) {
        lap_count = kCounterCap;
    }

    lock_counters();
    Preferences prefs;
    if (open_prefs(&prefs)) {
        uint32_t races = clamp_counter(prefs.getUInt("races", 0));
        uint32_t laps = clamp_counter(prefs.getUInt("laps", 0));
        if (races < kCounterCap) {
            races++;
        }
        if (laps + lap_count > kCounterCap) {
            laps = kCounterCap;
        } else {
            laps += lap_count;
        }
        prefs.putUInt("races", races);
        prefs.putUInt("laps", laps);
        prefs.end();
    }
    unlock_counters();
}

static uint32_t read_counter(Preferences *prefs, const char *key) {
    return clamp_counter(prefs->getUInt(key, 0));
}

static void fill_counters(Preferences *prefs, SfCounters *out) {
    out->poweron = read_counter(prefs, "poweron");
    out->panic = read_counter(prefs, "panic");
    out->wdt = read_counter(prefs, "wdt");
    out->brownout = read_counter(prefs, "brown");
    out->other = read_counter(prefs, "other");
    out->races = read_counter(prefs, "races");
    out->laps = read_counter(prefs, "laps");
}

bool sf_telemetry_build(char *buf, size_t len, SfCounters *snap) {
    if (!buf || len == 0 || !snap) {
        return false;
    }

    lock_counters();
    Preferences prefs;
    bool opened = open_prefs(&prefs);
    if (opened) {
        fill_counters(&prefs, snap);
        prefs.end();
    } else {
        memset(snap, 0, sizeof(*snap));
    }
    unlock_counters();

    const char *last_result = "BOOT_OK";
    if (esp_reset_reason() == ESP_RST_PANIC) {
        last_result = "PANIC";
    }

    StaticJsonDocument<768> doc;
    JsonObject state = doc.createNestedObject("state");
    JsonObject reported = state.createNestedObject("reported");
    reported["fw_version"] = SF_FW_VERSION;
    reported["uptime_s"] = static_cast<uint32_t>(esp_timer_get_time() / 1000000LL);
    reported["heap_low"] = static_cast<uint32_t>(esp_get_minimum_free_heap_size());

    JsonObject resets = reported.createNestedObject("resets");
    resets["poweron"] = snap->poweron;
    resets["panic"] = snap->panic;
    resets["wdt"] = snap->wdt;
    resets["brownout"] = snap->brownout;
    resets["other"] = snap->other;

    JsonObject usage = reported.createNestedObject("usage");
    usage["races"] = snap->races;
    usage["laps"] = snap->laps;
    reported["last_result"] = last_result;

    size_t written = serializeJson(doc, buf, len);
    if (written == 0 || written >= len) {
        buf[0] = '\0';
        return false;
    }
    return true;
}

static void sub_counter(Preferences *prefs, const char *key, uint32_t published) {
    uint32_t current = clamp_counter(prefs->getUInt(key, 0));
    published = clamp_counter(published);
    uint32_t next = current > published ? current - published : 0;
    prefs->putUInt(key, next);
}

void sf_telemetry_commit(const SfCounters *published) {
    if (!published) {
        return;
    }

    lock_counters();
    Preferences prefs;
    if (open_prefs(&prefs)) {
        sub_counter(&prefs, "poweron", published->poweron);
        sub_counter(&prefs, "panic", published->panic);
        sub_counter(&prefs, "wdt", published->wdt);
        sub_counter(&prefs, "brown", published->brownout);
        sub_counter(&prefs, "other", published->other);
        sub_counter(&prefs, "races", published->races);
        sub_counter(&prefs, "laps", published->laps);
        prefs.end();
    }
    unlock_counters();
}

#endif
