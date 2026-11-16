#include "aws_iot.h"

#if defined(BOARD_ESP32_S3_TOUCH)

#include <Arduino.h>
#include <atomic>
#include <stdio.h>
#include <string.h>
#include "telemetry.h"
#include "mqtt_client.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

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
        case MQTT_EVENT_DISCONNECTED:
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

static void session(esp_mqtt_client_handle_t client) {
    int64_t last_pub_us = -kPublishIntervalUs;
    while (s_up.load()) {
        int64_t now = esp_timer_get_time();
        if (now - last_pub_us >= kPublishIntervalUs) {
            publish_once(client);
            last_pub_us = esp_timer_get_time();
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
}

static bool start_client(esp_mqtt_client_handle_t *client, bool *running) {
    esp_err_t err;
    if (!*running) {
        err = esp_mqtt_client_start(*client);
        *running = (err == ESP_OK);
    } else {
        err = esp_mqtt_client_reconnect(*client);
        if (err != ESP_OK) {
            esp_mqtt_client_stop(*client);
            *running = false;
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
        vTaskDelete(nullptr);
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
        vTaskDelete(nullptr);
        return;
    }

    s_events = xEventGroupCreate();
    if (!s_events) {
        Serial.println("AWS MQTT task not started");
        vTaskDelete(nullptr);
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

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&cfg);
    if (!client) {
        Serial.println("AWS MQTT task not started");
        vEventGroupDelete(s_events);
        s_events = nullptr;
        vTaskDelete(nullptr);
        return;
    }
    esp_mqtt_client_register_event(client, MQTT_EVENT_ANY, mqtt_event, nullptr);

    bool running = false;
    int attempt = 0;
    while (attempt < kMaxAttempts) {
        attempt++;
        Serial.printf("AWS MQTT connect attempt %d\n", attempt);
        xEventGroupClearBits(s_events, BIT_CONNECTED | BIT_DISCONNECTED);
        s_up.store(false);

        if (!start_client(&client, &running)) {
            if (attempt >= kMaxAttempts) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(backoff_ms(attempt)));
            continue;
        }

        EventBits_t bits = xEventGroupWaitBits(
            s_events, BIT_CONNECTED | BIT_DISCONNECTED, pdFALSE, pdFALSE, pdMS_TO_TICKS(20000));

        if ((bits & BIT_CONNECTED) && s_up.load()) {
            Serial.printf("MQTT connected, client id %s\n", s_creds->thing_name);
            xEventGroupClearBits(s_events, BIT_DISCONNECTED);
            session(client);
        } else if (!(bits & BIT_DISCONNECTED) && running) {
            esp_mqtt_client_stop(client);
            running = false;
        }

        if (attempt >= kMaxAttempts) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(backoff_ms(attempt)));
    }

    Serial.printf("AWS MQTT stopped after %d attempts\n", attempt);
    if (running) {
        esp_mqtt_client_stop(client);
    }
    esp_mqtt_client_destroy(client);
    vEventGroupDelete(s_events);
    s_events = nullptr;
    vTaskDelete(nullptr);
}

void sf_aws_start(const SfAwsCreds *creds) {
    static bool started = false;
    if (started) {
        return;
    }
    started = true;
    s_creds = creds;

    BaseType_t ok = xTaskCreatePinnedToCore(aws_task, "sf_aws", 8192, nullptr, 5, nullptr, 0);
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
