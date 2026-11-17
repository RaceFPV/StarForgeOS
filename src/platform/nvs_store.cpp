#include "platform/nvs_store.h"

#include "nvs.h"
#include "nvs_flash.h"

NvsStore::NvsStore() : handle_(0), open_(false) {}

bool NvsStore::open(const char* name, bool read_only) {
    close();
    nvs_open_mode_t mode = read_only ? NVS_READONLY : NVS_READWRITE;
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(name, mode, &handle);
    if (err != ESP_OK) {
        return false;
    }
    handle_ = handle;
    open_ = true;
    return true;
}

void NvsStore::close() {
    if (open_) {
        nvs_close((nvs_handle_t)handle_);
        open_ = false;
        handle_ = 0;
    }
}

uint8_t NvsStore::getUChar(const char* key, uint8_t fallback) {
    uint8_t value = fallback;
    if (open_) {
        nvs_get_u8((nvs_handle_t)handle_, key, &value);
    }
    return value;
}

int8_t NvsStore::getChar(const char* key, int8_t fallback) {
    int8_t value = fallback;
    if (open_) {
        nvs_get_i8((nvs_handle_t)handle_, key, &value);
    }
    return value;
}

uint32_t NvsStore::getUInt(const char* key, uint32_t fallback) {
    uint32_t value = fallback;
    if (open_) {
        nvs_get_u32((nvs_handle_t)handle_, key, &value);
    }
    return value;
}

size_t NvsStore::putUChar(const char* key, uint8_t value) {
    if (!open_ || nvs_set_u8((nvs_handle_t)handle_, key, value) != ESP_OK) {
        return 0;
    }
    return nvs_commit((nvs_handle_t)handle_) == ESP_OK ? sizeof(value) : 0;
}

size_t NvsStore::putChar(const char* key, int8_t value) {
    if (!open_ || nvs_set_i8((nvs_handle_t)handle_, key, value) != ESP_OK) {
        return 0;
    }
    return nvs_commit((nvs_handle_t)handle_) == ESP_OK ? sizeof(value) : 0;
}

size_t NvsStore::putUInt(const char* key, uint32_t value) {
    if (!open_ || nvs_set_u32((nvs_handle_t)handle_, key, value) != ESP_OK) {
        return 0;
    }
    return nvs_commit((nvs_handle_t)handle_) == ESP_OK ? sizeof(value) : 0;
}

void NvsStore::remove(const char* key) {
    if (!open_) {
        return;
    }
    nvs_erase_key((nvs_handle_t)handle_, key);
    nvs_commit((nvs_handle_t)handle_);
}

bool NvsStore::isKey(const char* key) {
    if (!open_) {
        return false;
    }
    uint8_t unused = 0;
    esp_err_t err = nvs_get_u8((nvs_handle_t)handle_, key, &unused);
    return err != ESP_ERR_NVS_NOT_FOUND;
}
