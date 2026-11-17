#pragma once

#include <cstddef>
#include <sys/types.h>

// Mount the "spiffs" partition at /spiffs. Paths passed to the helpers are
// web paths such as "/index.html"; the helpers prefix /spiffs.

bool spiffs_mount(bool format_if_failed);
bool spiffs_ready();
size_t spiffs_total_bytes();
size_t spiffs_used_bytes();
bool spiffs_exists(const char* web_path);
size_t spiffs_file_size(const char* web_path);
void spiffs_vfs_path(const char* web_path, char* out, size_t out_len);
