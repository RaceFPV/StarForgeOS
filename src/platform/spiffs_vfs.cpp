#include "platform/spiffs_vfs.h"

#include <cstdio>
#include <cstring>

#include "esp_spiffs.h"

static bool mounted = false;

void spiffs_vfs_path(const char* web_path, char* out, size_t out_len) {
    if (!web_path || web_path[0] == '\0') {
        snprintf(out, out_len, "/spiffs");
        return;
    }
    if (web_path[0] == '/') {
        snprintf(out, out_len, "/spiffs%s", web_path);
    } else {
        snprintf(out, out_len, "/spiffs/%s", web_path);
    }
}

bool spiffs_mount(bool format_if_failed) {
    if (mounted) {
        return true;
    }
    esp_vfs_spiffs_conf_t conf = {};
    conf.base_path = "/spiffs";
    conf.partition_label = "spiffs";
    conf.max_files = 8;
    conf.format_if_mount_failed = format_if_failed;
    mounted = esp_vfs_spiffs_register(&conf) == ESP_OK;
    return mounted;
}

bool spiffs_ready() {
    return mounted;
}

size_t spiffs_total_bytes() {
    size_t total = 0;
    size_t used = 0;
    if (!mounted || esp_spiffs_info("spiffs", &total, &used) != ESP_OK) {
        return 0;
    }
    return total;
}

size_t spiffs_used_bytes() {
    size_t total = 0;
    size_t used = 0;
    if (!mounted || esp_spiffs_info("spiffs", &total, &used) != ESP_OK) {
        return 0;
    }
    return used;
}

bool spiffs_exists(const char* web_path) {
    char path[128];
    spiffs_vfs_path(web_path, path, sizeof(path));
    FILE* file = fopen(path, "rb");
    if (!file) {
        return false;
    }
    fclose(file);
    return true;
}

size_t spiffs_file_size(const char* web_path) {
    char path[128];
    spiffs_vfs_path(web_path, path, sizeof(path));
    FILE* file = fopen(path, "rb");
    if (!file) {
        return 0;
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fclose(file);
    return size > 0 ? (size_t)size : 0;
}
