#ifndef SF_AWS_IOT_H
#define SF_AWS_IOT_H

#include "fs_creds.h"

// Start the Phase 1 AWS IoT task. Returns immediately and never blocks setup or loop.
// creds is null when the files were missing. The pointed-to buffers must stay
// allocated for the process lifetime. The task does not touch SPIFFS.
void sf_aws_start(const SfAwsCreds *creds);

#if defined(BOARD_ESP32_S3_TOUCH)
// Root CA already loaded for MQTT. The OTA download uses the same PEM so
// verification stays on. Null if credentials were not loaded.
const char *sf_aws_root_ca(void);
int sf_aws_publish_qos1(void *client, const char *topic, const char *payload);
bool sf_aws_puback_is(int msg_id);
#endif

#endif
