#include "battery_monitor.h"
#include "config/config_globals.h"

#include <cmath>

#if ENABLE_BATTERY_MONITOR && defined(BATTERY_ADC_PIN)

BatteryMonitor::BatteryMonitor() {
    // Constructor
}

void BatteryMonitor::begin() {
    // Initialize ADC for battery monitoring
    // Match PhobosLT's simple approach - just set pin as INPUT
    hal_gpio_input(g_battery_adc_pin);

#if defined(USB_DETECT_PIN)
    // Initialize USB detection pin (D+ line monitoring)
    hal_gpio_input(g_usb_detect_pin);
    log_fmt("USB detection enabled on GPIO%d (connect D+ -> 100kΩ -> GPIO%d)\n",
                  g_usb_detect_pin, g_usb_detect_pin);
#endif
}

float BatteryMonitor::readVoltage() {
    // Read ADC value (averaging for stability)
    // Match PhobosLT's approach: use moving average for better stability
    static uint16_t measurements[BATTERY_SAMPLES] = {0};
    static uint8_t measurement_index = 0;
    static uint32_t average_sum = 0;
    static bool initialized = false;
    
    if (!initialized) {
        // Initialize with first readings
        for (int i = 0; i < BATTERY_SAMPLES; i++) {
            int adc_value = hal_adc_read(g_battery_adc_pin);
            if (adc_value >= 0 && adc_value <= 4095) {
                measurements[i] = adc_value;
                average_sum += adc_value;
            }
            hal_delay_ms(1);
        }
        initialized = true;
    }
    
    // Read new value
    int adc_value = hal_adc_read(g_battery_adc_pin);
    if (adc_value < 0 || adc_value > 4095) {
        adc_value = 0;  // Invalid reading, use 0
    }
    
    // Update moving average (like PhobosLT)
    average_sum = average_sum - measurements[measurement_index];  // Subtract oldest
    measurements[measurement_index] = adc_value;                  // Replace with new
    average_sum += adc_value;                                     // Add new to sum
    measurement_index = (measurement_index + 1) % BATTERY_SAMPLES;
    
    // Calculate average
    uint16_t avg_adc = average_sum / BATTERY_SAMPLES;

    // PhobosLT method: map ADC (0-4095) to scaled value (0 to 33*scale) + offset
    // scale = 2, so: hal_map(0-4095, 0, 66) + offset
    // This accounts for 3.3V ref, 2:1 divider, and voltage drop compensation
    // Use round() like PhobosLT does for better accuracy
    uint8_t scaled = hal_map(round(avg_adc), 0, 4095, 0, 33 * (uint8_t)BATTERY_VOLTAGE_DIVIDER);
    
    #if defined(BATTERY_ADC_OFFSET)
        scaled += BATTERY_ADC_OFFSET;  // Add calibration offset (e.g., +2 for T-Energy)
    #endif
    
    // Convert to voltage: scaled value is in 0.1V units (e.g., 42 = 4.2V)
    float battery_voltage = (float)scaled / 10.0;

    return battery_voltage;
}

uint8_t BatteryMonitor::calculatePercentage(float voltage) {
    // Simple linear mapping (good enough for most LiPo cells)
    // For better accuracy, use a LiPo discharge curve lookup table
    if (voltage >= BATTERY_MAX_VOLTAGE) return 100;
    if (voltage <= BATTERY_MIN_VOLTAGE) return 0;

    float percentage = ((voltage - BATTERY_MIN_VOLTAGE) / (BATTERY_MAX_VOLTAGE - BATTERY_MIN_VOLTAGE)) * 100.0;
    return (uint8_t)percentage;
}

#if defined(USB_DETECT_PIN)
bool BatteryMonitor::isUSBConnected() {
    // USB detection via D+ line monitoring
    // D+ is normally HIGH (~3.3V) when USB is connected, LOW when disconnected
    // Sample multiple times to avoid false readings from USB data traffic
    int highCount = 0;

    // Take multiple samples to filter out data pulses
    for (int i = 0; i < USB_DETECT_SAMPLES; i++) {
        if (hal_gpio_read(g_usb_detect_pin) == 1) {
            highCount++;
        }
        hal_delay_us(100);  // Small delay between samples to avoid catching same pulse
    }

    // Majority vote: If more than half the samples are HIGH, USB is connected
    // This filters out transient data pulses while reliably detecting USB presence
    return (highCount > (USB_DETECT_SAMPLES / 2));
}
#endif

#endif // ENABLE_BATTERY_MONITOR && defined(BATTERY_ADC_PIN)
