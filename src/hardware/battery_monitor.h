#ifndef BATTERY_MONITOR_H
#define BATTERY_MONITOR_H

#include "platform/hal.h"
#include "config/config.h"

#if ENABLE_BATTERY_MONITOR && defined(BATTERY_ADC_PIN)

// Battery voltage monitoring with voltage divider on ADC1 pin
// Supports optional USB charging detection via D+ line monitoring
class BatteryMonitor {
public:
    BatteryMonitor();

    // Initialize battery monitoring hardware
    void begin();

    // Read current battery voltage (averaged for stability)
    float readVoltage();

    // Calculate battery percentage from voltage (0-100%)
    uint8_t calculatePercentage(float voltage);

#if defined(USB_DETECT_PIN)
    // Detect if USB is connected (charging indicator)
    // Uses D+ line monitoring with majority vote filtering
    bool isUSBConnected();
#endif

private:
    // Note: BATTERY_SAMPLES and USB_DETECT_SAMPLES are defined in config.h
};

#endif // ENABLE_BATTERY_MONITOR && defined(BATTERY_ADC_PIN)

#endif // BATTERY_MONITOR_H
