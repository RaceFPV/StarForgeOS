#ifndef SF_TELEMETRY_H
#define SF_TELEMETRY_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Counters are cumulative since the last PUBACK. A publish snapshots them;
// commit subtracts that snapshot so a race that finishes while the publish
// is in flight is kept for the next report.
struct SfCounters {
    uint32_t poweron;
    uint32_t panic;
    uint32_t wdt;
    uint32_t brownout;
    uint32_t other;
    uint32_t races;
    uint32_t laps;
};

#if defined(BOARD_ESP32_S3_TOUCH)

void sf_telemetry_boot(void);
void sf_usage_race_completed(uint32_t lap_count);
bool sf_telemetry_build(char *buf, size_t len, SfCounters *snap);
void sf_telemetry_commit(const SfCounters *published);

#else

inline void sf_telemetry_boot(void) {}
inline void sf_usage_race_completed(uint32_t) {}
inline bool sf_telemetry_build(char *, size_t, SfCounters *) { return false; }
inline void sf_telemetry_commit(const SfCounters *) {}

#endif

#endif
