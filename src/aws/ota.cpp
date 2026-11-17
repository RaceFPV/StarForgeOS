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
// Apply ordering: re-fetch jobs/<jobId>/get on the MQTT connection that is
// already up, copy the presigned URL into PSRAM, then destroy that client
// and only then open the HTTPS download. A second handshake before the
// download does not fit in internal RAM. SUCCEEDED is published on a fresh
// connection after the slot is the boot partition. No cross-session resume
// and no Range header: a failed apply starts at byte 0 next time. The check
// path does not report IN_PROGRESS and does not download.

#include "aws_iot.h"
#include "config/config.h"
#include "fs_creds.h"

#include <ArduinoJson.h>
#include <esp_http_client.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
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
static const uint32_t kHealthyDelayMs = 3000;

static std::atomic<bool> s_race{false};
static std::atomic<bool> s_confirmed{false};
static bool s_mark_done = false;
static uint32_t s_loop_ms = 0;

static char s_thing[64];
static char s_job_id[80];
static char s_version[32];
static char s_sha[80];
static int s_size_bytes = 0;
// The job document stays in the receive buffer. These point at it until apply.
static const char *s_doc_json = nullptr;
static int s_doc_json_len = 0;
static StaticJsonDocument<6144> s_job_doc;

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
static OtaMsg s_held;
static std::atomic<bool> s_inbox_ready{false};
static SemaphoreHandle_t s_inbox_lock = nullptr;
static char s_asm_topic[192];
// Reassembly runs before apply, so this cannot wait until then. The prebuilt
// SDK leaves EXT_RAM_BSS_ATTR empty, so PSRAM has to be an explicit allocation.
static char *s_asm_body = nullptr;
static int s_asm_len = 0;

// Offer and apply are RAM-only. Nothing about "update available" is stored.
static char s_offer[32];
static std::atomic<bool> s_offer_on{false};
static std::atomic<int> s_progress{-1};
static std::atomic<bool> s_apply_request{false};
static bool s_applying = false;
static int s_fail_pct = 0;

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

static int report_status(esp_mqtt_client_handle_t client, const char *status, const char *details) {
    char payload[320];
    snprintf(payload, sizeof(payload), "{\"status\":\"%s\",\"statusDetails\":%s}", status, details);
    return publish_job(client, payload);
}

static void log_progress(int pct, int written, int total) {
    unsigned wkb = static_cast<unsigned>(written / 1024);
    unsigned tkb = static_cast<unsigned>(total > 0 ? total / 1024 : written / 1024);
    Serial.printf("download %d%% (%u KB / %u KB)\n", pct, wkb, tkb);
}

// Pull the presigned URL out of the job document still sitting in the receive
// buffer. The copy lives only for this attempt.
static char *copy_url_for_apply(int *size_bytes) {
    *size_bytes = 0;
    if (!s_doc_json || s_doc_json_len <= 0) {
        return nullptr;
    }
    if (deserializeJson(s_job_doc, s_doc_json, static_cast<size_t>(s_doc_json_len))) {
        return nullptr;
    }
    JsonObject body = s_job_doc["execution"]["jobDocument"];
    if (body.isNull()) {
        s_job_doc.clear();
        return nullptr;
    }
    const char *url = body["url"] | "";
    *size_bytes = body["size_bytes"] | 0;
    size_t n = strlen(url);
    if (n == 0 || n >= static_cast<size_t>(kMsgBody)) {
        s_job_doc.clear();
        return nullptr;
    }
    char *copy = static_cast<char *>(heap_caps_malloc(n + 1, MALLOC_CAP_SPIRAM));
    if (!copy) {
        s_job_doc.clear();
        Serial.println("job url not allocated");
        return nullptr;
    }
    memcpy(copy, url, n + 1);
    s_job_doc.clear();
    return copy;
}

static const char *release_download(esp_http_client_handle_t http, char *url, uint8_t *chunk,
                                    esp_ota_handle_t ota, bool abort_ota, const char *reason) {
    if (abort_ota) {
        esp_ota_abort(ota);
    }
    if (http) {
        esp_http_client_close(http);
        esp_http_client_cleanup(http);
    }
    if (url) {
        heap_caps_free(url);
    }
    if (chunk) {
        heap_caps_free(chunk);
    }
    return reason;
}

// Called only after the MQTT client has been destroyed. The chunk comes from
// PSRAM before the HTTP client is created, so it is not competing with the
// handshake. One attempt, from byte 0. No Range header.
const char *sf_ota_stream(char *url, int size_bytes) {
    s_fail_pct = 0;
    s_size_bytes = size_bytes;
    uint8_t *chunk = static_cast<uint8_t *>(heap_caps_malloc(kChunk, MALLOC_CAP_SPIRAM));
    if (!chunk) {
        Serial.println("download buffer not allocated");
        if (url) {
            heap_caps_free(url);
        }
        return "ota_write_failed";
    }

    const char *root_ca = sf_aws_root_ca();
    esp_http_client_config_t http_cfg = {};
    http_cfg.url = url;
    http_cfg.cert_pem = root_ca;
    http_cfg.cert_len = 0;
    http_cfg.timeout_ms = 10000;
    // Staging buffer only. The bytes written to flash are the PSRAM chunk.
    // A 4 KB internal buffer here would be taken before ssl_setup.
    http_cfg.buffer_size = 1024;
    http_cfg.method = HTTP_METHOD_GET;
    http_cfg.disable_auto_redirect = true;

    esp_http_client_handle_t http = esp_http_client_init(&http_cfg);
    if (!http) {
        return release_download(nullptr, url, chunk, 0, false, "timeout");
    }

    sf_aws_log_heap("apply start");
    int64_t started = esp_timer_get_time();
    if (esp_http_client_open(http, 0) != ESP_OK) {
        return release_download(http, url, chunk, 0, false, "timeout");
    }
    (void)esp_http_client_fetch_headers(http);
    int http_status = esp_http_client_get_status_code(http);
    if (http_status != 200) {
        Serial.printf("job %s: http status %d\n", s_job_id, http_status);
        return release_download(http, url, chunk, 0, false, "http_status");
    }

    int total = s_size_bytes;
    if (total <= 0) {
        int64_t len = esp_http_client_get_content_length(http);
        if (len > 0 && len < 0x7fffffff) {
            total = static_cast<int>(len);
        }
    }

    const esp_partition_t *part = esp_ota_get_next_update_partition(nullptr);
    esp_ota_handle_t ota = 0;
    esp_err_t begin_err = part ? esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &ota) : ESP_FAIL;
    if (!part || begin_err != ESP_OK) {
        const char *reason = (begin_err == ESP_ERR_OTA_ROLLBACK_INVALID_STATE) ? "reboot_pending"
                                                                                : "ota_begin_failed";
        return release_download(http, url, chunk, 0, false, reason);
    }

    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    bool sha_ok = mbedtls_sha256_starts(&sha, 0) == 0;
    int written = 0;
    int next_mark = 25;
    const char *fail_reason = nullptr;

    while (sha_ok && !fail_reason) {
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
            s_progress.store(pct);
            s_fail_pct = pct;
            while (next_mark <= 100 && pct >= next_mark) {
                log_progress(next_mark, written, total);
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
    heap_caps_free(url);
    heap_caps_free(chunk);

    if (fail_reason) {
        esp_ota_abort(ota);
        return fail_reason;
    }
    if (got[0] == '\0' || !same_hash(got, s_sha)) {
        esp_ota_abort(ota);
        if (s_fail_pct <= 0) {
            s_fail_pct = 100;
        }
        return "hash_mismatch";
    }

    esp_err_t end_err = esp_ota_end(ota);
    sf_aws_log_heap("after ota end");
    if (end_err != ESP_OK) {
        return "image_invalid";
    }
    if (esp_ota_set_boot_partition(part) != ESP_OK) {
        return "reboot_pending";
    }
    s_progress.store(100);
    return nullptr;
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
    s_doc_json = nullptr;
    s_doc_json_len = 0;
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
    if (deserializeJson(s_job_doc, json, static_cast<size_t>(len))) {
        if (from_next && s_doc == DOC_WAIT_NEXT) {
            s_doc = DOC_IDLE;
        }
        return;
    }
    JsonObject exec = s_job_doc["execution"];
    if (exec.isNull()) {
        s_job_doc.clear();
        if (from_next && s_doc == DOC_WAIT_NEXT) {
            s_doc = DOC_IDLE;
        }
        return;
    }
    const char *job_id = exec["jobId"] | "";
    const char *status = exec["status"] | "";
    if (job_id[0] == '\0' || !actionable(status)) {
        s_job_doc.clear();
        if (s_doc == DOC_WAIT_NEXT || s_doc == DOC_WAIT_JOB) {
            s_doc = DOC_IDLE;
        }
        return;
    }
    remember_job(job_id);
    if (from_next) {
        // $next/get mints a URL too, but the spec fetch is jobs/<jobId>/get.
        // Drop whatever document arrived here and fetch that one next.
        s_job_doc.clear();
        if (s_doc == DOC_WAIT_NEXT) {
            s_doc = DOC_IDLE;
        }
        return;
    }

    JsonObject body = exec["jobDocument"];
    if (body.isNull()) {
        s_job_doc.clear();
        return;
    }
    // Check time keeps only the identity of the image. The URL stays in the
    // receive buffer and is copied out at apply time.
    const char *version = body["version"] | "";
    const char *sha = body["sha256"] | "";
    snprintf(s_version, sizeof(s_version), "%s", version);
    snprintf(s_sha, sizeof(s_sha), "%s", sha);
    s_doc_json = json;
    s_doc_json_len = len;
    s_have_doc = true;
    s_doc = DOC_IDLE;
    s_job_doc.clear();
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
    if (!s_asm_body) {
        s_asm_body = static_cast<char *>(heap_caps_malloc(kMsgBody, MALLOC_CAP_SPIRAM));
        if (!s_asm_body) {
            Serial.println("job document does not fit");
            return;
        }
    }
    if (offset == 0) {
        memcpy(s_asm_topic, topic, static_cast<size_t>(topic_len));
        s_asm_topic[topic_len] = '\0';
        s_asm_len = 0;
    }
    if (total_len < 0 || total_len >= kMsgBody || offset < 0 || data_len < 0 ||
        offset + data_len > kMsgBody) {
        Serial.printf("job document does not fit (%d bytes)\n", total_len);
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

    // The job document stays in s_held until apply copies the URL out. A newer
    // MQTT message waits in the inbox rather than overwriting it.
    bool hold_doc = s_have_doc && !s_attempted && s_doc_json != nullptr;
    if (!hold_doc && s_inbox_lock && xSemaphoreTake(s_inbox_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
        bool got = false;
        if (s_inbox_ready.load()) {
            s_held = s_inbox;
            s_inbox_ready.store(false);
            got = true;
        }
        xSemaphoreGive(s_inbox_lock);
        if (got) {
            handle_msg(&s_held);
        }
    }

    // Apply owns the next document. The check path must not report or offer
    // over the top of that wait.
    if (s_applying) {
        return;
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
    if (strcmp(s_version, SF_FW_VERSION) == 0) {
        s_doc_json = nullptr;
        s_doc_json_len = 0;
        report_status(client, "SUCCEEDED", "{\"note\":\"already-current\"}");
        Serial.printf("job %s: SUCCEEDED\n", s_job_id);
        s_attempted = true;
        s_offer_on.store(false);
        return;
    }
    // QUEUED until the user confirms. The URL in the receive buffer is ignored.
    if (!s_offer_on.load()) {
        snprintf(s_offer, sizeof(s_offer), "%s", s_version);
        s_offer_on.store(true);
    }
}

const char *sf_ota_offer_version(void) {
    return s_offer_on.load() ? s_offer : "";
}

int sf_ota_progress_pct(void) {
    return s_progress.load();
}

void sf_ota_request_apply(void) {
    if (!s_offer_on.load() || s_applying) {
        return;
    }
    s_apply_request.store(true);
}

bool sf_ota_take_apply_request(void) {
    return s_apply_request.exchange(false);
}

bool sf_ota_blocked_by_pending(void) {
    return pending_verify() && !s_confirmed.load();
}

int sf_ota_report(esp_mqtt_client_handle_t client, const char *status, const char *details) {
    if (status && strcmp(status, "IN_PROGRESS") == 0) {
        Serial.printf("job %s: IN_PROGRESS\n", s_job_id);
    }
    return report_status(client, status, details);
}

bool sf_ota_version_is_current(void) {
    return s_version[0] != '\0' && strcmp(s_version, SF_FW_VERSION) == 0;
}

void sf_ota_mark_current(void) {
    s_doc_json = nullptr;
    s_doc_json_len = 0;
    s_attempted = true;
    s_offer_on.store(false);
}

void sf_ota_report_already_current(esp_mqtt_client_handle_t client) {
    report_status(client, "SUCCEEDED", "{\"note\":\"already-current\"}");
    Serial.printf("job %s: SUCCEEDED\n", s_job_id);
    sf_ota_mark_current();
}

bool sf_ota_refetch_url(esp_mqtt_client_handle_t client, char **url, int *size_bytes) {
    if (url) {
        *url = nullptr;
    }
    if (size_bytes) {
        *size_bytes = 0;
    }
    if (!client || !url || !size_bytes || s_job_id[0] == '\0') {
        return false;
    }
    s_applying = true;
    s_fail_pct = 0;
    s_have_doc = false;
    s_doc_json = nullptr;
    s_doc_json_len = 0;
    request_document(client);
    int64_t start = esp_timer_get_time();
    while (esp_timer_get_time() - start < kDocWaitUs) {
        sf_ota_poll(client);
        if (s_have_doc && s_doc_json) {
            *url = copy_url_for_apply(size_bytes);
            return *url != nullptr;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return false;
}

const char *sf_ota_precheck(const char *url, int size_bytes) {
    if (!url || strncmp(url, "https://", 8) != 0) {
        return "no_url";
    }
    if (!hex64(s_sha)) {
        return "hash_mismatch";
    }
    const esp_partition_t *part = esp_ota_get_next_update_partition(nullptr);
    if (!part) {
        return "ota_begin_failed";
    }
    if (size_bytes > 0 && static_cast<size_t>(size_bytes) > part->size) {
        return "image_invalid";
    }
    const char *root = sf_aws_root_ca();
    if (!root || root[0] == '\0') {
        return "timeout";
    }
    return nullptr;
}

int sf_ota_report_failed(esp_mqtt_client_handle_t client, const char *reason) {
    if (!reason) {
        reason = "timeout";
    }
    char details[80];
    if (s_fail_pct > 0) {
        snprintf(details, sizeof(details), "{\"reason\":\"%s\",\"pct\":\"%d\"}", reason, s_fail_pct);
    } else {
        snprintf(details, sizeof(details), "{\"reason\":\"%s\"}", reason);
    }
    Serial.printf("job %s: FAILED %s\n", s_job_id, reason);
    if (!client) {
        return -1;
    }
    return report_status(client, "FAILED", details);
}

int sf_ota_report_succeeded(esp_mqtt_client_handle_t client) {
    char details[96];
    snprintf(details, sizeof(details), "{\"version\":\"%s\",\"reboot\":\"true\"}", s_version);
    Serial.printf("job %s: SUCCEEDED\n", s_job_id);
    if (!client) {
        return -1;
    }
    return report_status(client, "SUCCEEDED", details);
}

void sf_ota_set_progress(int pct) {
    s_progress.store(pct);
}

void sf_ota_end_apply(void) {
    s_applying = false;
    s_progress.store(-1);
}

#endif
