#ifndef CONFIG_LOADER_H
#define CONFIG_LOADER_H

#include <Arduino.h>

// Custom pin configuration structure
// These override the defaults from config.h if enabled
struct CustomPinConfig {
    bool enabled = false;               // If true, use custom pins; if false, use config.h defaults
    
    // Core RX5808 and RSSI pins (required for all boards)
    uint8_t rssi_input_pin = 0;         // RSSI input from RX5808 (ADC)
    uint8_t rx5808_data_pin = 0;        // SPI MOSI to RX5808
    uint8_t rx5808_clk_pin = 0;         // SPI SCK to RX5808
    uint8_t rx5808_sel_pin = 0;         // SPI CS to RX5808
    uint8_t mode_switch_pin = 0;        // Mode selection switch
    
    // Optional pins (LCD boards)
    uint8_t power_button_pin = 0;       // Power button (long press = sleep)
    uint8_t battery_adc_pin = 0;        // Battery voltage monitoring
    uint8_t audio_dac_pin = 0;          // Audio output
    uint8_t usb_detect_pin = 0;         // USB detection (D+ line monitoring for charging indicator)
    
    // LCD/Touch pins (advanced users with custom LCD setups)
    int8_t lcd_i2c_sda = -1;            // Touch I2C SDA (-1 = not used)
    int8_t lcd_i2c_scl = -1;            // Touch I2C SCL
    int8_t lcd_backlight = -1;          // Backlight control
};

class ConfigLoader {
public:
    // Load configuration from SPIFFS /config.json
    // Returns true if custom config loaded successfully and enabled
    // Returns false if no config, parsing error, or disabled (use config.h defaults)
    static bool loadCustomConfig(CustomPinConfig* config);
    
    // Save configuration to SPIFFS /config.json (for future: web UI config editor)
    static bool saveCustomConfig(const CustomPinConfig* config);
    
    // Check if custom config exists and is enabled
    static bool hasCustomConfig();
    
private:
    static const char* CONFIG_FILE_PATH;
};

#endif // CONFIG_LOADER_H

