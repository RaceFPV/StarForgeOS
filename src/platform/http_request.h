#pragma once

#include <cstdlib>
#include <cstring>

#include "esp_http_server.h"

// Small wrapper so route handlers can read form fields and send a body
// through the ESP-IDF HTTP server.
class HttpRequest {
public:
    struct Value {
        int number;
        int toInt() const { return number; }
    };

    struct Param {
        int number;
        Value value() const { return Value{number}; }
    };

    explicit HttpRequest(httpd_req_t* request) : req_(request), cached_{0}, loaded_(false) {
        body_[0] = '\0';
    }

    bool hasParam(const char* key, bool /*post_body*/) {
        int unused = 0;
        return find(key, &unused);
    }

    const Param* getParam(const char* key, bool /*post_body*/) {
        if (!find(key, &cached_.number)) {
            return nullptr;
        }
        return &cached_;
    }

    void send(int code, const char* type, const char* body) {
        httpd_resp_set_status(req_, status_text(code));
        httpd_resp_set_type(req_, type);
        httpd_resp_send(req_, body ? body : "", HTTPD_RESP_USE_STRLEN);
    }

    httpd_req_t* raw() const { return req_; }

private:
    httpd_req_t* req_;
    Param cached_;
    char body_[1024];
    bool loaded_;

    static const char* status_text(int code) {
        switch (code) {
            case 200: return "200 OK";
            case 400: return "400 Bad Request";
            case 404: return "404 Not Found";
            case 500: return "500 Internal Server Error";
            case 501: return "501 Not Implemented";
            default: return "500 Internal Server Error";
        }
    }

    void load() {
        if (loaded_) {
            return;
        }
        loaded_ = true;
        int received = httpd_req_recv(req_, body_, sizeof(body_) - 1);
        if (received > 0) {
            body_[received] = '\0';
        } else {
            body_[0] = '\0';
        }
    }

    bool find(const char* key, int* out) {
        load();
        const char* sources[2] = {body_, nullptr};
        char query[256];
        if (httpd_req_get_url_query_str(req_, query, sizeof(query)) == ESP_OK) {
            sources[1] = query;
        }
        size_t key_len = std::strlen(key);
        for (const char* source : sources) {
            if (!source) {
                continue;
            }
            const char* cursor = source;
            while (*cursor) {
                if (std::strncmp(cursor, key, key_len) == 0 && cursor[key_len] == '=') {
                    *out = std::atoi(cursor + key_len + 1);
                    return true;
                }
                const char* next = std::strchr(cursor, '&');
                if (!next) {
                    break;
                }
                cursor = next + 1;
            }
        }
        return false;
    }
};
