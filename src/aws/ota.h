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

#else

inline void sf_ota_service_tick(bool) {}

#endif
