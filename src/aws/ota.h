#pragma once

#include <stdbool.h>

#if defined(BOARD_ESP32_S3_TOUCH)

struct esp_mqtt_client;

// Called from the main loop. Records whether a race is running and, once
// setup has been up for a few seconds, confirms the running image so the
// bootloader does not roll it back.
void sf_ota_service_tick(bool race_active);

// MQTT session hooks. All job and download work runs on the AWS task.
void sf_ota_on_mqtt_connect(struct esp_mqtt_client *client, const char *thing_name);
void sf_ota_on_mqtt_data(const char *topic, int topic_len, const char *data, int data_len,
                         int total_len, int offset);
void sf_ota_on_mqtt_disconnect(void);
void sf_ota_poll(struct esp_mqtt_client *client);

// Idle-screen prompt. Empty when this session has no newer job.
const char *sf_ota_offer_version(void);
// -1 when an apply is not running. Otherwise the download percent.
int sf_ota_progress_pct(void);
// LCD confirm. Ignored unless a version is currently offered.
void sf_ota_request_apply(void);
bool sf_ota_take_apply_request(void);

// Apply steps. The AWS task owns the MQTT client and calls these in order.
bool sf_ota_blocked_by_pending(void);
int sf_ota_report(struct esp_mqtt_client *client, const char *status, const char *details);
bool sf_ota_version_is_current(void);
void sf_ota_mark_current(void);
void sf_ota_report_already_current(struct esp_mqtt_client *client);
// Re-fetch jobs/<jobId>/get on the connection that is already up.
// On success, *url is a PSRAM copy the caller owns.
bool sf_ota_refetch_url(struct esp_mqtt_client *client, char **url, int *size_bytes);
const char *sf_ota_precheck(const char *url, int size_bytes);
// Frees url. Null means the inactive slot is the boot partition.
// A non-null return is a stable failure reason. Aborts the OTA handle
// when one is still open.
const char *sf_ota_stream(char *url, int size_bytes);
int sf_ota_report_failed(struct esp_mqtt_client *client, const char *reason);
int sf_ota_report_succeeded(struct esp_mqtt_client *client);
void sf_ota_set_progress(int pct);
void sf_ota_end_apply(void);

#else

inline void sf_ota_service_tick(bool) {}

#endif
