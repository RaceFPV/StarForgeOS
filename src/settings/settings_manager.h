#ifndef SETTINGS_MANAGER_H
#define SETTINGS_MANAGER_H

#include "platform/hal.h"
#include "timing_core.h"

// Settings persistence manager using ESP32 NvsStore (NVS)
// Handles loading and saving user settings to non-volatile storage
class SettingsManager {
public:
    SettingsManager();

    // Load settings from NVS and apply to timing core
    // Returns true if settings were loaded successfully
    bool loadSettings(TimingCore* timingCore);

    // Save current settings from timing core to NVS
    // Returns true if settings were saved successfully
    bool saveSettings(TimingCore* timingCore);

private:
    static const uint32_t MAGIC_NUMBER = 0x53464F53;  // "SFOS" in hex
};

#endif // SETTINGS_MANAGER_H
