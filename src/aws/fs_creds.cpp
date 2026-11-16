#include "fs_creds.h"

#if defined(BOARD_ESP32_S3_TOUCH)

#include <Arduino.h>
#include <SPIFFS.h>
#include <string.h>
#include "esp_heap_caps.h"

static const size_t kMaxCredBytes = 8192;

static char *alloc_buf(size_t bytes) {
    char *buf = static_cast<char *>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!buf) {
        buf = static_cast<char *>(malloc(bytes));
    }
    return buf;
}

static void free_creds(SfAwsCreds *out) {
    free(out->cert_pem);
    free(out->key_pem);
    free(out->ca_pem);
    free(out->endpoint);
    free(out->thing_name);
    free(out->broker_uri);
    memset(out, 0, sizeof(*out));
}

static char *read_file(const char *path, size_t *out_len, bool trim_trailing) {
    File file = SPIFFS.open(path, "r");
    if (!file) {
        return nullptr;
    }

    size_t size = file.size();
    if (size == 0 || size > kMaxCredBytes) {
        file.close();
        return nullptr;
    }

    char *buf = alloc_buf(size + 1);
    if (!buf) {
        file.close();
        return nullptr;
    }

    size_t got = file.readBytes(buf, size);
    file.close();
    buf[got] = '\0';

    if (trim_trailing) {
        while (got > 0) {
            char c = buf[got - 1];
            if (c != '\n' && c != '\r' && c != ' ' && c != '\t') {
                break;
            }
            buf[--got] = '\0';
        }
    }

    if (out_len) {
        *out_len = got;
    }
    return buf;
}

bool sf_fs_creds_load(SfAwsCreds *out) {
    memset(out, 0, sizeof(*out));

    out->cert_pem = read_file("/aws/cert.pem", &out->cert_len, false);
    out->key_pem = read_file("/aws/device.key", &out->key_len, false);
    out->ca_pem = read_file("/aws/root-CA.crt", &out->ca_len, false);
    out->endpoint = read_file("/aws/endpoint.txt", nullptr, true);
    out->thing_name = read_file("/aws/thing.txt", nullptr, true);

    if (!out->cert_pem || !out->key_pem || !out->ca_pem ||
        !out->endpoint || out->endpoint[0] == '\0' ||
        !out->thing_name || out->thing_name[0] == '\0') {
        free_creds(out);
        return false;
    }

    if (strchr(out->endpoint, ' ') || strchr(out->endpoint, '/') || strchr(out->thing_name, ' ')) {
        free_creds(out);
        return false;
    }

    size_t uri_len = strlen("mqtts://") + strlen(out->endpoint) + strlen(":8883") + 1;
    out->broker_uri = alloc_buf(uri_len);
    if (!out->broker_uri) {
        free_creds(out);
        return false;
    }
    snprintf(out->broker_uri, uri_len, "mqtts://%s:8883", out->endpoint);
    return true;
}

#else

bool sf_fs_creds_load(SfAwsCreds *out) {
    if (out) {
        out->cert_pem = nullptr;
        out->key_pem = nullptr;
        out->ca_pem = nullptr;
        out->endpoint = nullptr;
        out->thing_name = nullptr;
        out->broker_uri = nullptr;
        out->cert_len = 0;
        out->key_len = 0;
        out->ca_len = 0;
    }
    return false;
}

#endif
