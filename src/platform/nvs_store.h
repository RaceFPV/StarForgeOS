#pragma once

#include <cstddef>
#include <cstdint>

// NVS key/value store. Same records the previous Preferences namespace used,
// so existing pin and settings data still loads.
class NvsStore {
public:
    NvsStore();
    bool open(const char* name, bool read_only);
    void close();

    uint8_t getUChar(const char* key, uint8_t fallback);
    int8_t getChar(const char* key, int8_t fallback);
    uint32_t getUInt(const char* key, uint32_t fallback);

    size_t putUChar(const char* key, uint8_t value);
    size_t putChar(const char* key, int8_t value);
    size_t putUInt(const char* key, uint32_t value);
    void remove(const char* key);
    bool isKey(const char* key);

private:
    uint32_t handle_;
    bool open_;
};
