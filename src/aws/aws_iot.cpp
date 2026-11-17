#include "aws_iot.h"

#if defined(BOARD_ESP32_S3_TOUCH)

#include <Arduino.h>
#include <atomic>
#include <stdio.h>
#include <string.h>
#include "telemetry.h"
#include "ota.h"
#include "standalone_mode.h"
#include "mqtt_client.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"

static const int kMaxAttempts = 8;
static const int64_t kPublishIntervalUs = 300LL * 1000 * 1000;
static const char *kWill = "{\"state\":{\"reported\":{\"online\":false}}}";

static const EventBits_t BIT_CONNECTED = 1u << 0;
static const EventBits_t BIT_DISCONNECTED = 1u << 1;

static EventGroupHandle_t s_events = nullptr;
static std::atomic<bool> s_up{false};
static std::atomic<int> s_pub_ack{-1};
static char s_shadow_topic[160];
static const SfAwsCreds *s_creds = nullptr;

static void log_heap(const char *where) {
#if defined(SF_DEBUG_HEAP)
    Serial.printf(
        "heap %s: internal free %u, internal largest %u, internal min free %u, psram free %u\n",
        where,
        static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
        static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
        static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)),
        static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
#else
    (void)where;
#endif
}

void sf_aws_log_heap(const char *where) {
    log_heap(where);
}

static uint32_t backoff_ms(int attempt) {
    static const uint32_t kDelaysMs[] = {5000, 10000, 20000, 40000, 60000};
    int index = attempt - 1;
    if (index < 0) {
        index = 0;
    }
    if (index > 4) {
        index = 4;
    }
    return kDelaysMs[index];
}

static void mqtt_event(void *arg, esp_event_base_t base, int32_t event_id, void *event_data) {
    (void)arg;
    (void)base;
    esp_mqtt_event_handle_t event = static_cast<esp_mqtt_event_handle_t>(event_data);

    switch (static_cast<esp_mqtt_event_id_t>(event_id)) {
        case MQTT_EVENT_CONNECTED:
            s_up.store(true);
            if (s_events) {
                xEventGroupSetBits(s_events, BIT_CONNECTED);
            }
            break;
        case MQTT_EVENT_ERROR:
            log_heap("mqtt error");
            break;
        case MQTT_EVENT_DISCONNECTED:
            log_heap("mqtt disconnected");
            s_up.store(false);
            if (s_events) {
                xEventGroupSetBits(s_events, BIT_DISCONNECTED);
            }
            break;
        case MQTT_EVENT_PUBLISHED:
            if (event) {
                s_pub_ack.store(event->msg_id);
            }
            break;
        case MQTT_EVENT_DATA:
            if (event) {
                sf_ota_on_mqtt_data(event->topic, event->topic_len, event->data, event->data_len,
                                    event->total_data_len, event->current_data_offset);
            }
            break;
        default:
            break;
    }
}

static void publish_once(esp_mqtt_client_handle_t client) {
    SfCounters snap;
    char payload[512];
    if (!sf_telemetry_build(payload, sizeof(payload), &snap)) {
        Serial.println("AWS shadow payload not built");
        return;
    }

    s_pub_ack.store(-1);
    int msg_id = esp_mqtt_client_publish(client, s_shadow_topic, payload,
                                         static_cast<int>(strlen(payload)), 1, 0);
    if (msg_id < 0) {
        Serial.println("AWS shadow publish failed");
        return;
    }

    Serial.printf("shadow publish sent (msg id %d)\n", msg_id);
    static bool logged_publish = false;
    if (!logged_publish) {
        logged_publish = true;
        log_heap("after shadow publish");
    }
    for (int i = 0; i < 30 && s_up.load(); i++) {
        if (s_pub_ack.load() == msg_id) {
            Serial.println("PUBACK");
            sf_telemetry_commit(&snap);
            Serial.println("counters cleared");
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

static bool start_client(esp_mqtt_client_handle_t *client, bool *running);

static esp_mqtt_client_handle_t make_client(const esp_mqtt_client_config_t *cfg) {
    esp_mqtt_client_handle_t client = esp_mqtt_client_init(cfg);
    if (!client) {
        return nullptr;
    }
    esp_mqtt_client_register_event(client, MQTT_EVENT_ANY, mqtt_event, nullptr);
    return client;
}

static bool connect_once(esp_mqtt_client_handle_t *client, bool *running) {
    xEventGroupClearBits(s_events, BIT_CONNECTED | BIT_DISCONNECTED);
    s_up.store(false);
    if (!client || !*client || !start_client(client, running)) {
        return false;
    }
    EventBits_t bits = xEventGroupWaitBits(
        s_events, BIT_CONNECTED | BIT_DISCONNECTED, pdFALSE, pdFALSE, pdMS_TO_TICKS(20000));
    if ((bits & BIT_CONNECTED) && s_up.load()) {
        xEventGroupClearBits(s_events, BIT_DISCONNECTED);
        return true;
    }
    if (*running && !(bits & BIT_DISCONNECTED)) {
        esp_mqtt_client_stop(*client);
        *running = false;
    }
    return false;
}

static void wait_puback(int msg_id) {
    if (msg_id < 0) {
        return;
    }
    int64_t start = esp_timer_get_time();
    while (esp_timer_get_time() - start < 3000000) {
        if (sf_aws_puback_is(msg_id)) {
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void fail_before_download(esp_mqtt_client_handle_t client, const char *reason, char *url) {
    if (url) {
        heap_caps_free(url);
    }
    sf_ota_set_progress(-1);
    if (reason && strcmp(reason, "already-current") == 0) {
        sf_ota_report_already_current(client);
    } else {
        sf_ota_report_failed(client, reason ? reason : "timeout");
    }
    sf_ota_end_apply();
    StandaloneMode::releaseTimingAfterUpdate();
}

// True when the inactive slot is the boot partition and the caller must reboot.
// The URL is re-fetched before the client is destroyed. Reconnecting first
// would spend the only internal TLS arena on a handshake the download needs.
static bool run_apply(esp_mqtt_client_handle_t *client, bool *running,
                      const esp_mqtt_client_config_t *cfg) {
    StandaloneMode::holdTimingForUpdate();
    sf_ota_set_progress(0);

    if (sf_ota_blocked_by_pending()) {
        fail_before_download(*client, "reboot_pending", nullptr);
        return false;
    }

    sf_ota_report(*client, "IN_PROGRESS", "{\"step\":\"starting\"}");

    char *url = nullptr;
    int size_bytes = 0;
    if (!sf_ota_refetch_url(*client, &url, &size_bytes)) {
        fail_before_download(*client, "timeout", url);
        return false;
    }
    if (sf_ota_version_is_current()) {
        fail_before_download(*client, "already-current", url);
        return false;
    }
    const char *pre = sf_ota_precheck(url, size_bytes);
    if (pre) {
        fail_before_download(*client, pre, url);
        return false;
    }

    sf_ota_on_mqtt_disconnect();
    if (*running) {
        esp_mqtt_client_stop(*client);
        *running = false;
    }
    esp_mqtt_client_destroy(*client);
    *client = nullptr;
    s_up.store(false);

    const char *reason = sf_ota_stream(url, size_bytes);
    bool flashed = (reason == nullptr);

    *client = make_client(cfg);
    *running = false;
    bool connected = *client && connect_once(client, running);
    if (connected) {
        Serial.printf("MQTT connected, client id %s\n", s_creds->thing_name);
        log_heap("mqtt connected");
        sf_ota_on_mqtt_connect(*client, s_creds->thing_name);
    }

    if (flashed) {
        int msg_id = connected ? sf_ota_report_succeeded(*client) : -1;
        if (!connected) {
            Serial.printf("job report not sent\n");
        }
        wait_puback(msg_id);
        return true;
    }

    sf_ota_set_progress(-1);
    if (connected) {
        sf_ota_report_failed(*client, reason);
    } else {
        sf_ota_report_failed(nullptr, reason);
    }
    sf_ota_end_apply();
    StandaloneMode::releaseTimingAfterUpdate();
    return false;
}

static bool session(esp_mqtt_client_handle_t *client, bool *running,
                    const esp_mqtt_client_config_t *cfg) {
    bool shadow_sent = false;
    int64_t last_pub_us = -kPublishIntervalUs;
    sf_ota_on_mqtt_connect(*client, s_creds->thing_name);
    while (s_up.load()) {
        int64_t now = esp_timer_get_time();
        if (now - last_pub_us >= kPublishIntervalUs) {
            publish_once(*client);
            last_pub_us = esp_timer_get_time();
            shadow_sent = true;
        }
        if (shadow_sent) {
            sf_ota_poll(*client);
            if (sf_ota_take_apply_request()) {
                if (run_apply(client, running, cfg)) {
                    return true;
                }
            }
        }

        if (!s_events) {
            break;
        }
        EventBits_t bits = xEventGroupWaitBits(
            s_events, BIT_DISCONNECTED, pdTRUE, pdFALSE, pdMS_TO_TICKS(1000));
        if (bits & BIT_DISCONNECTED) {
            break;
        }
    }
    sf_ota_on_mqtt_disconnect();
    return false;
}

static bool start_client(esp_mqtt_client_handle_t *client, bool *running) {
    esp_err_t err;
    if (!*running) {
        log_heap("before mqtt start");
        err = esp_mqtt_client_start(*client);
        *running = (err == ESP_OK);
    } else {
        err = esp_mqtt_client_reconnect(*client);
        if (err != ESP_OK) {
            esp_mqtt_client_stop(*client);
            *running = false;
            log_heap("before mqtt start");
            err = esp_mqtt_client_start(*client);
            *running = (err == ESP_OK);
        }
    }
    return err == ESP_OK;
}

static void aws_task(void *param) {
    (void)param;

    if (!s_creds || !s_creds->cert_pem || !s_creds->key_pem || !s_creds->ca_pem ||
        !s_creds->broker_uri || !s_creds->thing_name) {
        Serial.println("AWS credentials missing, MQTT disabled");
        vTaskDeleteWithCaps(nullptr);
        return;
    }

    Serial.printf("AWS credentials loaded: cert=%u key=%u ca=%u\n",
                  static_cast<unsigned>(s_creds->cert_len),
                  static_cast<unsigned>(s_creds->key_len),
                  static_cast<unsigned>(s_creds->ca_len));

    int topic_len = snprintf(s_shadow_topic, sizeof(s_shadow_topic),
                             "$aws/things/%s/shadow/update", s_creds->thing_name);
    if (topic_len <= 0 || topic_len >= static_cast<int>(sizeof(s_shadow_topic))) {
        Serial.println("AWS credentials missing, MQTT disabled");
        vTaskDeleteWithCaps(nullptr);
        return;
    }

    s_events = xEventGroupCreate();
    if (!s_events) {
        Serial.println("AWS MQTT task not started");
        vTaskDeleteWithCaps(nullptr);
        return;
    }

    esp_mqtt_client_config_t cfg = {};
    cfg.broker.address.uri = s_creds->broker_uri;
    cfg.broker.verification.certificate = s_creds->ca_pem;
    cfg.credentials.client_id = s_creds->thing_name;
    cfg.credentials.authentication.certificate = s_creds->cert_pem;
    cfg.credentials.authentication.key = s_creds->key_pem;
    cfg.session.keepalive = 60;
    cfg.session.last_will.topic = s_shadow_topic;
    cfg.session.last_will.msg = kWill;
    cfg.session.last_will.qos = 1;
    cfg.session.last_will.retain = 0;
    cfg.network.reconnect_timeout_ms = 5000;
    cfg.network.timeout_ms = 10000;
    cfg.network.disable_auto_reconnect = true;
    cfg.task.priority = 5;
    cfg.task.stack_size = 8192;
    // Allocated at start, before the handshake, out of the same internal pool.
    // 3072 holds a job document with a ~900 byte presigned URL plus its topic.
    // 1024 is enough for status publishes. A document that does not fit is
    // logged and dropped; it is not a crash.
    cfg.buffer.size = 3072;
    cfg.buffer.out_size = 1024;

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&cfg);
    if (!client) {
        Serial.println("AWS MQTT task not started");
        vEventGroupDelete(s_events);
        s_events = nullptr;
        vTaskDeleteWithCaps(nullptr);
        return;
    }
    esp_mqtt_client_register_event(client, MQTT_EVENT_ANY, mqtt_event, nullptr);

    bool running = false;
    int attempt = 0;
    while (attempt < kMaxAttempts) {
        attempt++;
        if (!client) {
            client = make_client(&cfg);
            running = false;
            if (!client) {
                break;
            }
        }
        Serial.printf("AWS MQTT connect attempt %d\n", attempt);
        if (!connect_once(&client, &running)) {
            if (attempt >= kMaxAttempts) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(backoff_ms(attempt)));
            continue;
        }

        Serial.printf("MQTT connected, client id %s\n", s_creds->thing_name);
        log_heap("mqtt connected");
        if (session(&client, &running, &cfg)) {
            esp_restart();
        }

        if (attempt >= kMaxAttempts) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(backoff_ms(attempt)));
    }

    Serial.printf("AWS MQTT stopped after %d attempts\n", attempt);
    if (running && client) {
        esp_mqtt_client_stop(client);
    }
    if (client) {
        esp_mqtt_client_destroy(client);
    }
    vEventGroupDelete(s_events);
    s_events = nullptr;
    vTaskDeleteWithCaps(nullptr);
}

const char *sf_aws_root_ca(void) {
    if (!s_creds || !s_creds->ca_pem) {
        return nullptr;
    }
    return s_creds->ca_pem;
}

int sf_aws_publish_qos1(void *client, const char *topic, const char *payload) {
    if (!client || !topic || !payload) {
        return -1;
    }
    s_pub_ack.store(-1);
    return esp_mqtt_client_publish(static_cast<esp_mqtt_client_handle_t>(client), topic, payload,
                                   static_cast<int>(strlen(payload)), 1, 0);
}

bool sf_aws_puback_is(int msg_id) {
    return msg_id >= 0 && s_pub_ack.load() == msg_id;
}

void sf_aws_start(const SfAwsCreds *creds) {
    static bool started = false;
    if (started) {
        return;
    }
    started = true;
    s_creds = creds;

    // The MQTT TLS handshake runs on the MQTT client's own task
    // (cfg.task.stack_size), not on aws_task. This stack is for the apply
    // path (document parse, HTTP read, SHA-256) and lives in PSRAM.
    BaseType_t ok = xTaskCreatePinnedToCoreWithCaps(
        aws_task, "sf_aws", 24576, nullptr, 5, nullptr, 0,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ok != pdPASS) {
        started = false;
        Serial.println("AWS MQTT task not started");
    }
}

#else

void sf_aws_start(const SfAwsCreds *creds) {
    (void)creds;
}

#endif
