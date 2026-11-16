#include "ota.h"

#if defined(BOARD_ESP32_S3_TOUCH)

// Phase 2 OTA. One attempt per MQTT session, always from byte 0.
//
// No cross-session resume: a failed or deferred download is not continued.
// The next boot fetches the job again and starts at the first byte. Nothing
// is written to NVS about the offset. Resumed transfers wait until there is
// real failure data; this phase does not send a Range header.
//
// The image is never buffered and never written to SPIFFS. Each read is 4 KB,
// handed to esp_ota_write and to the running SHA-256. TLS verification stays
// on, using the Amazon root CA already loaded for MQTT. SHA-256 only — there
// is no signature check in this phase.
//
// A download does not start while a race is active. If a race starts after
// the download has begun, the partial slot is aborted and the job is left
// IN_PROGRESS so the next boot can try again from byte 0.

#include "aws_iot.h"
#include "config/config.h"
#include "fs_creds.h"

#include <ArduinoJson.h>
#include <esp_http_client.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <mbedtls/sha256.h>
#include <mqtt_client.h>
#include <freertos/semphr.h>

#include <atomic>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const int kChunk = 4096;
static const int kMsgBody = 8192;
static const int64_t kDownloadBudgetUs = 5LL * 60 * 1000 * 1000;
static const int64_t kDocWaitUs = 15LL * 1000 * 1000;
static const int64_t kConfirmWaitUs = 30LL * 1000 * 1000;
static const uint32_t kHealthyDelayMs = 3000;

static std::atomic<bool> s_race{false};
static std::atomic<bool> s_confirmed{false};
static bool s_mark_done = false;
static uint32_t s_loop_ms = 0;

static char s_thing[64];
static char s_job_id[80];
static char s_version[32];
static char s_sha[80];
static char s_url[3072];
static int s_size_bytes = 0;

static bool s_subscribed = false;
static bool s_kicked = false;
static bool s_attempted = false;
static bool s_have_doc = false;
static bool s_defer_logged = false;
static int64_t s_doc_deadline = 0;

enum DocWait : uint8_t { DOC_IDLE = 0, DOC_WAIT_NEXT, DOC_WAIT_JOB };
static DocWait s_doc = DOC_IDLE;

// One inbound MQTT message. The MQTT task only copies into this static slot;
// an 8 KB object on that task's stack would overflow it.
struct OtaMsg {
    char topic[192];
    char body[kMsgBody];
    int body_len;
};

static OtaMsg s_inbox;
static std::atomic<bool> s_inbox_ready{false};
static SemaphoreHandle_t s_inbox_lock = nullptr;
static char s_asm_topic[192];
static char s_asm_body[kMsgBody];
static int s_asm_len = 0;

static bool hex64(const char *s) {
    if (!s) {
        return false;
    }
    size_t n = strlen(s);
    if (n != 64) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (!ok) {
            return false;
        }
    }
    return true;
}

static bool same_hash(const char *got, const char *want) {
    for (int i = 0; i < 64; i++) {
        char a = got[i];
        char b = want[i];
        if (a >= 'A' && a <= 'F') {
            a = static_cast<char>(a - 'A' + 'a');
        }
        if (b >= 'A' && b <= 'F') {
            b = static_cast<char>(b - 'A' + 'a');
        }
        if (a != b) {
            return false;
        }
    }
    return true;
}

static void to_hex(const uint8_t *in, char *out) {
    static const char *kHex = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        out[i * 2] = kHex[in[i] >> 4];
        out[i * 2 + 1] = kHex[in[i] & 0x0f];
    }
    out[64] = '\0';
}

static bool pending_verify(void) {
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (!running || esp_ota_get_state_partition(running, &state) != ESP_OK) {
        return false;
    }
    return state == ESP_OTA_IMG_PENDING_VERIFY;
}

static int publish_job(esp_mqtt_client_handle_t client, const char *payload) {
    char topic[192];
    snprintf(topic, sizeof(topic), "$aws/things/%s/jobs/%s/update", s_thing, s_job_id);
    int msg_id = sf_aws_publish_qos1(client, topic, payload);
    if (msg_id >= 0) {
        // The reboot path waits a fixed 3 s. Progress reports only need the
        // publish to be queued; the MQTT task delivers the QoS 1 packet.
        (void)msg_id;
    }
    return msg_id;
}

static void report_status(esp_mqtt_client_handle_t client, const char *status, const char *details) {
    char payload[320];
    snprintf(payload, sizeof(payload), "{\"status\":\"%s\",\"statusDetails\":%s}", status, details);
    publish_job(client, payload);
}

static bool image_ready_or_wait(void) {
    if (!pending_verify() || s_confirmed.load()) {
        return !pending_verify() || s_confirmed.load();
    }
    int64_t start = esp_timer_get_time();
    while (esp_timer_get_time() - start < kConfirmWaitUs) {
        if (s_race.load()) {
            return false;
        }
        if (s_confirmed.load() || !pending_verify()) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    return s_confirmed.load() || !pending_verify();
}

static void log_progress(int pct, int written, int total) {
    unsigned wkb = static_cast<unsigned>(written / 1024);
    unsigned tkb = static_cast<unsigned>(total > 0 ? total / 1024 : written / 1024);
    Serial.printf("download %d%% (%u KB / %u KB)\n", pct, wkb, tkb);
}

static void download_and_flash(esp_mqtt_client_handle_t client) {
    s_attempted = true;
    Serial.printf("job %s: IN_PROGRESS\n", s_job_id);
    report_status(client, "IN_PROGRESS", "{\"step\":\"starting\"}");

    if (strcmp(s_version, SF_FW_VERSION) == 0) {
        report_status(client, "SUCCEEDED", "{\"note\":\"already-current\"}");
        Serial.printf("job %s: SUCCEEDED\n", s_job_id);
        return;
    }

    if (s_url[0] == '\0' || strncmp(s_url, "https://", 8) != 0) {
        report_status(client, "FAILED", "{\"reason\":\"no_url\"}");
        Serial.printf("job %s: FAILED no_url\n", s_job_id);
        return;
    }
    if (!hex64(s_sha)) {
        report_status(client, "FAILED", "{\"reason\":\"hash_mismatch\"}");
        Serial.printf("job %s: FAILED hash_mismatch\n", s_job_id);
        return;
    }

    const esp_partition_t *part = esp_ota_get_next_update_partition(nullptr);
    if (!part) {
        report_status(client, "FAILED", "{\"reason\":\"ota_begin_failed\"}");
        Serial.printf("job %s: FAILED ota_begin_failed\n", s_job_id);
        return;
    }
    if (s_size_bytes > 0 && static_cast<size_t>(s_size_bytes) > part->size) {
        report_status(client, "FAILED", "{\"reason\":\"image_invalid\"}");
        Serial.printf("job %s: FAILED image_invalid\n", s_job_id);
        return;
    }

    // cert_pem is the SPIFFS root CA. Leaving it unset, or skipping the name
    // check, would accept any certificate. The presigned URL carries its own
    // signature, so no Authorization header is added.
    const char *root_ca = sf_aws_root_ca();
    if (!root_ca || root_ca[0] == '\0') {
        report_status(client, "FAILED", "{\"reason\":\"timeout\"}");
        Serial.printf("job %s: FAILED timeout\n", s_job_id);
        return;
    }

    esp_http_client_config_t http_cfg = {};
    http_cfg.url = s_url;
    http_cfg.cert_pem = root_ca;
    http_cfg.cert_len = 0;
    http_cfg.timeout_ms = 10000;
    http_cfg.buffer_size = kChunk;
    http_cfg.method = HTTP_METHOD_GET;
    http_cfg.disable_auto_redirect = true;

    esp_http_client_handle_t http = esp_http_client_init(&http_cfg);
    if (!http) {
        report_status(client, "FAILED", "{\"reason\":\"timeout\"}");
        Serial.printf("job %s: FAILED timeout\n", s_job_id);
        return;
    }

    int64_t started = esp_timer_get_time();
    esp_err_t open_err = esp_http_client_open(http, 0);
    if (open_err != ESP_OK) {
        esp_http_client_cleanup(http);
        report_status(client, "FAILED", "{\"reason\":\"timeout\"}");
        Serial.printf("job %s: FAILED timeout\n", s_job_id);
        return;
    }
    (void)esp_http_client_fetch_headers(http);
    int http_status = esp_http_client_get_status_code(http);
    if (http_status != 200) {
        esp_http_client_close(http);
        esp_http_client_cleanup(http);
        report_status(client, "FAILED", "{\"reason\":\"http_status\"}");
        Serial.printf("job %s: FAILED http_status (%d)\n", s_job_id, http_status);
        return;
    }

    int total = s_size_bytes;
    if (total <= 0) {
        int64_t len = esp_http_client_get_content_length(http);
        if (len > 0 && len < 0x7fffffff) {
            total = static_cast<int>(len);
        }
    }

    esp_ota_handle_t ota = 0;
    esp_err_t begin_err = esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &ota);
    if (begin_err != ESP_OK) {
        esp_http_client_close(http);
        esp_http_client_cleanup(http);
        const char *reason = (begin_err == ESP_ERR_OTA_ROLLBACK_INVALID_STATE) ? "reboot_pending"
                                                                                : "ota_begin_failed";
        char details[64];
        snprintf(details, sizeof(details), "{\"reason\":\"%s\"}", reason);
        report_status(client, "FAILED", details);
        Serial.printf("job %s: FAILED %s\n", s_job_id, reason);
        return;
    }

    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    bool sha_ok = mbedtls_sha256_starts(&sha, 0) == 0;
    static uint8_t chunk[kChunk];
    int written = 0;
    int next_mark = 25;
    const char *fail_reason = nullptr;
    bool deferred = false;
    int fail_pct = 0;

    while (sha_ok && !fail_reason && !deferred) {
        if (s_race.load()) {
            // Not a terminal failure. The slot is discarded and the next boot
            // starts again at byte 0. See the file comment.
            Serial.printf("job %s: deferred, race active\n", s_job_id);
            deferred = true;
            break;
        }
        if (esp_timer_get_time() - started >= kDownloadBudgetUs) {
            fail_reason = "timeout";
            break;
        }
        int n = esp_http_client_read(http, reinterpret_cast<char *>(chunk), kChunk);
        if (n == -ESP_ERR_HTTP_EAGAIN) {
            continue;
        }
        if (n < 0) {
            fail_reason = "timeout";
            break;
        }
        if (n == 0) {
            if (esp_http_client_is_complete_data_received(http) || total <= 0) {
                break;
            }
            fail_reason = "timeout";
            break;
        }
        if (esp_ota_write(ota, chunk, static_cast<size_t>(n)) != ESP_OK) {
            fail_reason = "ota_write_failed";
            break;
        }
        if (mbedtls_sha256_update(&sha, chunk, static_cast<size_t>(n)) != 0) {
            sha_ok = false;
            fail_reason = "ota_write_failed";
            break;
        }
        written += n;
        if (total > 0) {
            int pct = static_cast<int>((static_cast<int64_t>(written) * 100) / total);
            if (pct > 100) {
                pct = 100;
            }
            while (next_mark <= 100 && pct >= next_mark) {
                log_progress(next_mark, written, total);
                char details[80];
                snprintf(details, sizeof(details),
                         "{\"step\":\"downloading\",\"pct\":\"%d\"}", next_mark);
                report_status(client, "IN_PROGRESS", details);
                fail_pct = next_mark;
                next_mark += 25;
            }
        }
    }

    uint8_t digest[32];
    char got[65];
    got[0] = '\0';
    if (sha_ok && mbedtls_sha256_finish(&sha, digest) == 0) {
        to_hex(digest, got);
    } else if (!fail_reason) {
        fail_reason = "ota_write_failed";
    }
    mbedtls_sha256_free(&sha);
    esp_http_client_close(http);
    esp_http_client_cleanup(http);

    if (deferred) {
        esp_ota_abort(ota);
        return;
    }
    if (fail_reason) {
        esp_ota_abort(ota);
        char details[80];
        if (fail_pct > 0) {
            snprintf(details, sizeof(details), "{\"reason\":\"%s\",\"pct\":\"%d\"}", fail_reason, fail_pct);
        } else {
            snprintf(details, sizeof(details), "{\"reason\":\"%s\"}", fail_reason);
        }
        report_status(client, "FAILED", details);
        Serial.printf("job %s: FAILED %s\n", s_job_id, fail_reason);
        return;
    }

    if (got[0] == '\0' || !same_hash(got, s_sha)) {
        esp_ota_abort(ota);
        char details[80];
        int pct = (total > 0) ? static_cast<int>((static_cast<int64_t>(written) * 100) / total) : 100;
        if (pct > 100) {
            pct = 100;
        }
        snprintf(details, sizeof(details), "{\"reason\":\"hash_mismatch\",\"pct\":\"%d\"}", pct);
        report_status(client, "FAILED", details);
        Serial.printf("job %s: FAILED hash_mismatch\n", s_job_id);
        return;
    }

    esp_err_t end_err = esp_ota_end(ota);
    if (end_err != ESP_OK) {
        report_status(client, "FAILED", "{\"reason\":\"image_invalid\"}");
        Serial.printf("job %s: FAILED image_invalid\n", s_job_id);
        return;
    }
    if (esp_ota_set_boot_partition(part) != ESP_OK) {
        report_status(client, "FAILED", "{\"reason\":\"reboot_pending\"}");
        Serial.printf("job %s: FAILED reboot_pending\n", s_job_id);
        return;
    }

    char details[96];
    snprintf(details, sizeof(details), "{\"version\":\"%s\",\"reboot\":\"true\"}", s_version);
    int msg_id = -1;
    char payload[320];
    snprintf(payload, sizeof(payload), "{\"status\":\"SUCCEEDED\",\"statusDetails\":%s}", details);
    char topic[192];
    snprintf(topic, sizeof(topic), "$aws/things/%s/jobs/%s/update", s_thing, s_job_id);
    msg_id = sf_aws_publish_qos1(client, topic, payload);
    Serial.printf("job %s: SUCCEEDED\n", s_job_id);

    int64_t wait_start = esp_timer_get_time();
    while (esp_timer_get_time() - wait_start < 3000000) {
        if (msg_id >= 0 && sf_aws_puback_is(msg_id)) {
            // Keep the rest of the 3 s so the broker can finish the PUBACK
            // path before the socket disappears.
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    esp_restart();
}

static void remember_job(const char *job_id) {
    if (!job_id || job_id[0] == '\0' || s_attempted) {
        return;
    }
    if (strcmp(job_id, "$next") == 0) {
        return;
    }
    if (s_job_id[0] != '\0' && strcmp(s_job_id, job_id) == 0 &&
        (s_have_doc || s_doc != DOC_IDLE)) {
        return;
    }
    snprintf(s_job_id, sizeof(s_job_id), "%s", job_id);
    s_have_doc = false;
    s_version[0] = '\0';
    s_sha[0] = '\0';
    s_url[0] = '\0';
    s_size_bytes = 0;
    s_defer_logged = false;
    if (s_doc == DOC_WAIT_NEXT) {
        s_doc = DOC_IDLE;
    }
}

static bool actionable(const char *status) {
    return strcmp(status, "QUEUED") == 0 || strcmp(status, "IN_PROGRESS") == 0;
}

static void take_document(const char *json, int len, bool from_next) {
    static StaticJsonDocument<6144> doc;
    if (deserializeJson(doc, json, static_cast<size_t>(len))) {
        if (from_next && s_doc == DOC_WAIT_NEXT) {
            s_doc = DOC_IDLE;
        }
        return;
    }
    JsonObject exec = doc["execution"];
    if (exec.isNull()) {
        if (from_next && s_doc == DOC_WAIT_NEXT) {
            s_doc = DOC_IDLE;
        }
        return;
    }
    const char *job_id = exec["jobId"] | "";
    const char *status = exec["status"] | "";
    if (job_id[0] == '\0' || !actionable(status)) {
        if (s_doc == DOC_WAIT_NEXT || s_doc == DOC_WAIT_JOB) {
            s_doc = DOC_IDLE;
        }
        return;
    }
    remember_job(job_id);
    if (from_next) {
        // $next/get mints a URL too, but the spec fetch is jobs/<jobId>/get.
        // Drop whatever document arrived here and fetch that one next.
        if (s_doc == DOC_WAIT_NEXT) {
            s_doc = DOC_IDLE;
        }
        return;
    }

    JsonObject body = exec["jobDocument"];
    if (body.isNull()) {
        return;
    }
    const char *version = body["version"] | "";
    const char *sha = body["sha256"] | "";
    const char *url = body["url"] | "";
    int size_bytes = body["size_bytes"] | 0;
    snprintf(s_version, sizeof(s_version), "%s", version);
    snprintf(s_sha, sizeof(s_sha), "%s", sha);
    if (strlen(url) >= sizeof(s_url)) {
        s_url[0] = '\0';
    } else {
        snprintf(s_url, sizeof(s_url), "%s", url);
    }
    s_size_bytes = size_bytes;
    s_have_doc = true;
    s_doc = DOC_IDLE;
}

static void note_rejected(const char *json, int len) {
    static StaticJsonDocument<512> doc;
    const char *message = "";
    if (!deserializeJson(doc, json, static_cast<size_t>(len))) {
        message = doc["message"] | "";
    }
    if (s_doc == DOC_WAIT_NEXT && s_job_id[0] == '\0') {
        s_doc = DOC_IDLE;
        return;
    }
    Serial.printf("job %s: rejected %s\n", s_job_id[0] ? s_job_id : "-", message);
    if (s_doc == DOC_WAIT_JOB || s_doc == DOC_WAIT_NEXT) {
        s_doc = DOC_IDLE;
        s_have_doc = false;
        if (s_job_id[0] != '\0') {
            s_attempted = true;
        }
    }
}

static bool topic_ends(const char *topic, const char *suffix) {
    size_t tlen = strlen(topic);
    size_t slen = strlen(suffix);
    return tlen >= slen && strcmp(topic + tlen - slen, suffix) == 0;
}

static void handle_msg(const OtaMsg *msg) {
    if (topic_ends(msg->topic, "/jobs/notify-next")) {
        static StaticJsonDocument<2048> doc;
        // The notification may carry a document whose URL is still the
        // placeholder. Only the job id is used here; the presigned URL comes
        // from jobs/<jobId>/get.
        if (deserializeJson(doc, msg->body, static_cast<size_t>(msg->body_len))) {
            return;
        }
        JsonObject exec = doc["execution"];
        if (exec.isNull()) {
            return;
        }
        const char *status = exec["status"] | "";
        if (!actionable(status)) {
            return;
        }
        remember_job(exec["jobId"] | "");
        return;
    }
    if (topic_ends(msg->topic, "/get/accepted")) {
        bool from_next = topic_ends(msg->topic, "/$next/get/accepted");
        take_document(msg->body, msg->body_len, from_next);
        return;
    }
    if (topic_ends(msg->topic, "/get/rejected") || topic_ends(msg->topic, "/update/rejected")) {
        note_rejected(msg->body, msg->body_len);
    }
}

static void request_document(esp_mqtt_client_handle_t client) {
    char topic[192];
    snprintf(topic, sizeof(topic), "$aws/things/%s/jobs/%s/get", s_thing, s_job_id);
    sf_aws_publish_qos1(client, topic, "{}");
    s_doc = DOC_WAIT_JOB;
    s_doc_deadline = esp_timer_get_time() + kDocWaitUs;
}

static void request_next(esp_mqtt_client_handle_t client) {
    char topic[192];
    snprintf(topic, sizeof(topic), "$aws/things/%s/jobs/$next/get", s_thing);
    sf_aws_publish_qos1(client, topic, "{}");
    s_doc = DOC_WAIT_NEXT;
    s_doc_deadline = esp_timer_get_time() + kDocWaitUs;
}

void sf_ota_service_tick(bool race_active) {
    s_race.store(race_active);
    if (s_mark_done) {
        return;
    }
    if (s_loop_ms == 0) {
        s_loop_ms = millis();
        return;
    }
    if (static_cast<uint32_t>(millis() - s_loop_ms) < kHealthyDelayMs) {
        return;
    }
    s_mark_done = true;
    // Setup has returned and the loop has been alive for a few seconds.
    // Calling this on every healthy boot is what stops a successful update
    // from being rolled back the next time power is pulled.
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    s_confirmed.store(true);
    if (err == ESP_OK) {
        Serial.println("OTA image marked valid, rollback cancelled");
    } else if (err == ESP_ERR_OTA_ROLLBACK_INVALID_STATE) {
        Serial.println("OTA image already valid");
    } else {
        Serial.printf("OTA mark valid failed: %s\n", esp_err_to_name(err));
    }
}

void sf_ota_on_mqtt_connect(esp_mqtt_client_handle_t client, const char *thing_name) {
    if (!thing_name) {
        return;
    }
    snprintf(s_thing, sizeof(s_thing), "%s", thing_name);
    if (!s_inbox_lock) {
        s_inbox_lock = xSemaphoreCreateMutex();
    }
    const char *tails[] = {
        "notify-next",
        "$next/get/accepted",
        "$next/get/rejected",
        "+/get/accepted",
        "+/get/rejected",
        "+/update/accepted",
        "+/update/rejected",
    };
    bool ok = true;
    for (const char *tail : tails) {
        char topic[192];
        snprintf(topic, sizeof(topic), "$aws/things/%s/jobs/%s", s_thing, tail);
        if (esp_mqtt_client_subscribe(client, topic, 1) < 0) {
            ok = false;
        }
    }
    s_subscribed = ok;
    s_kicked = false;
    if (!ok) {
        Serial.println("job subscribe failed");
    }
    vTaskDelay(pdMS_TO_TICKS(200));
}

void sf_ota_on_mqtt_disconnect(void) {
    s_subscribed = false;
    s_kicked = false;
    s_doc = DOC_IDLE;
}

void sf_ota_on_mqtt_data(const char *topic, int topic_len, const char *data, int data_len,
                         int total_len, int offset) {
    if (!topic || topic_len <= 0 || topic_len >= static_cast<int>(sizeof(s_asm_topic))) {
        return;
    }
    if (offset == 0) {
        memcpy(s_asm_topic, topic, static_cast<size_t>(topic_len));
        s_asm_topic[topic_len] = '\0';
        s_asm_len = 0;
    }
    if (total_len < 0 || total_len >= kMsgBody || offset < 0 || data_len < 0 ||
        offset + data_len > kMsgBody) {
        s_asm_len = 0;
        return;
    }
    if (data && data_len > 0) {
        memcpy(s_asm_body + offset, data, static_cast<size_t>(data_len));
        if (offset + data_len > s_asm_len) {
            s_asm_len = offset + data_len;
        }
    }
    if (s_asm_len < total_len) {
        return;
    }
    s_asm_body[s_asm_len] = '\0';
    if (!s_inbox_lock || xSemaphoreTake(s_inbox_lock, 0) != pdTRUE) {
        return;
    }
    if (!s_inbox_ready.load()) {
        snprintf(s_inbox.topic, sizeof(s_inbox.topic), "%s", s_asm_topic);
        s_inbox.body_len = s_asm_len;
        memcpy(s_inbox.body, s_asm_body, static_cast<size_t>(s_asm_len) + 1);
        s_inbox_ready.store(true);
    }
    xSemaphoreGive(s_inbox_lock);
}

void sf_ota_poll(esp_mqtt_client_handle_t client) {
    if (!client || !s_subscribed) {
        return;
    }
    if (!s_kicked && !s_attempted && s_job_id[0] == '\0') {
        // notify-next is not retained. A job queued while the timer was
        // powered off is discovered here; the document itself is still
        // fetched with jobs/<jobId>/get so the presigned URL is minted now.
        request_next(client);
        s_kicked = true;
    }

    if (s_inbox_lock && xSemaphoreTake(s_inbox_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
        static OtaMsg local;
        bool got = false;
        if (s_inbox_ready.load()) {
            local = s_inbox;
            s_inbox_ready.store(false);
            got = true;
        }
        xSemaphoreGive(s_inbox_lock);
        if (got) {
            handle_msg(&local);
        }
    }

    if (s_attempted) {
        return;
    }

    if ((s_doc == DOC_WAIT_NEXT || s_doc == DOC_WAIT_JOB) && !s_have_doc &&
        esp_timer_get_time() > s_doc_deadline) {
        DocWait waited = s_doc;
        s_doc = DOC_IDLE;
        if (waited == DOC_WAIT_JOB && s_job_id[0] != '\0') {
            Serial.printf("job %s: document not received\n", s_job_id);
            s_attempted = true;
        }
        return;
    }

    if (s_job_id[0] != '\0' && !s_have_doc && s_doc == DOC_IDLE) {
        if (s_race.load()) {
            if (!s_defer_logged) {
                Serial.printf("job %s: deferred, race active\n", s_job_id);
                s_defer_logged = true;
            }
            return;
        }
        s_defer_logged = false;
        request_document(client);
        return;
    }

    if (!s_have_doc) {
        return;
    }
    if (s_race.load()) {
        if (!s_defer_logged) {
            Serial.printf("job %s: deferred, race active\n", s_job_id);
            s_defer_logged = true;
        }
        return;
    }
    s_defer_logged = false;
    if (!image_ready_or_wait()) {
        if (s_race.load()) {
            return;
        }
        s_attempted = true;
        Serial.printf("job %s: FAILED reboot_pending\n", s_job_id);
        report_status(client, "FAILED", "{\"reason\":\"reboot_pending\"}");
        return;
    }
    download_and_flash(client);
}

#endif
