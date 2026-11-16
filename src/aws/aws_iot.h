#ifndef SF_AWS_IOT_H
#define SF_AWS_IOT_H

#include "fs_creds.h"

// Start the Phase 1 AWS IoT task. Returns immediately and never blocks setup or loop.
// creds is null when the files were missing. The pointed-to buffers must stay
// allocated for the process lifetime. The task does not touch SPIFFS.
void sf_aws_start(const SfAwsCreds *creds);

#endif
