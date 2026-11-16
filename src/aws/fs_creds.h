#ifndef SF_FS_CREDS_H
#define SF_FS_CREDS_H

#include <stddef.h>
#include <stdbool.h>

// SPIFFS identity for the AWS IoT client. PEM pointers are NULL-terminated
// and the length fields do not include that terminator.
// Buffers are allocated once and kept for the process lifetime.
struct SfAwsCreds {
    char *cert_pem;
    char *key_pem;
    char *ca_pem;
    char *endpoint;
    char *thing_name;
    char *broker_uri;
    size_t cert_len;
    size_t key_len;
    size_t ca_len;
};

// Reads /aws/* from an already-mounted SPIFFS. Does not call SPIFFS.begin().
// On success the buffers in out are never freed. On failure every pointer
// is NULL. Missing files are not an error for the timer.
bool sf_fs_creds_load(SfAwsCreds *out);

#endif
