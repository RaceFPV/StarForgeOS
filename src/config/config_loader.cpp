#include "config_loader.h"
#include "config_globals.h"
#include <Preferences.h>

const char* ConfigLoader::NVS_NAMESPACE = "sfos_pins";

bool ConfigLoader::loadCustomConfig(CustomPinConfig* config, bool allowSerialOutput) {
    if (!config) {
        return false;
    }
    
    Preferences prefs;
    
    // Try opening in read-write mode first (won't error if namespace doesn't exist)
    // This avoids the Preferences library error message on first boot
    if (!prefs.begin(NVS_NAMESPACE, false)) {
        // This should rarely happen - only if NVS is corrupted
        if (allowSerialOutput) {
            Serial.println("ConfigLoader: Failed to open NVS - using config.h defaults");
        }
        return false;
    }
    
    // Check if custom pins are enabled
    uint8_t enabled = prefs.getUChar("pin_enabled", 0);
    if (enabled == 0) {
        if (allowSerialOutput) {
            Serial.println("ConfigLoader: Custom pins disabled or not found - using config.h defaults");
        }
        prefs.end();
        return false;
    }
    
    // Load custom pin values
    config->enabled = true;
    
    // Core pins (required)
    config->rssi_input_pin = prefs.getUChar("pin_rssi_input", 0);
    config->rx5808_data_pin = prefs.getUChar("pin_rx5808_data", 0);
    config->rx5808_clk_pin = prefs.getUChar("pin_rx5808_clk", 0);
    config->rx5808_sel_pin = prefs.getUChar("pin_rx5808_sel", 0);
    config->mode_switch_pin = prefs.getUChar("pin_mode_switch", 0);
    
    // Optional pins (check if they exist in NVS, use 0 if not found)
    config->power_button_pin = prefs.getUChar("pin_power_button", 0);
    config->battery_adc_pin = prefs.getUChar("pin_battery_adc", 0);
    config->audio_dac_pin = prefs.getUChar("pin_audio_dac", 0);
    config->usb_detect_pin = prefs.getUChar("pin_usb_detect", 0);
    
    // LCD/Touch pins (use -1 as default, check if key exists)
    // Since Preferences doesn't have a direct "key exists" check for int8_t,
    // we'll use a magic value approach or check size
    int8_t lcd_sda = prefs.getChar("pin_lcd_i2c_sda", -1);
    int8_t lcd_scl = prefs.getChar("pin_lcd_i2c_scl", -1);
    int8_t lcd_bl = prefs.getChar("pin_lcd_backlight", -1);
    
    // Only set if not the default (-1) - indicates it was actually stored
    // Note: This means -1 can't be used as a valid pin, but that's fine since
    // -1 is the "not used" sentinel value anyway
    if (lcd_sda != -1 || prefs.isKey("pin_lcd_i2c_sda")) {
        config->lcd_i2c_sda = lcd_sda;
    } else {
        config->lcd_i2c_sda = -1;
    }
    
    if (lcd_scl != -1 || prefs.isKey("pin_lcd_i2c_scl")) {
        config->lcd_i2c_scl = lcd_scl;
    } else {
        config->lcd_i2c_scl = -1;
    }
    
    if (lcd_bl != -1 || prefs.isKey("pin_lcd_backlight")) {
        config->lcd_backlight = lcd_bl;
    } else {
        config->lcd_backlight = -1;
    }
    
    prefs.end();
    
    if (allowSerialOutput) {
        Serial.println("ConfigLoader: Custom pin configuration loaded from NVS!");
        Serial.printf("  RSSI Input: GPIO%d\n", config->rssi_input_pin);
        Serial.printf("  RX5808 Data: GPIO%d\n", config->rx5808_data_pin);
        Serial.printf("  RX5808 CLK: GPIO%d\n", config->rx5808_clk_pin);
        Serial.printf("  RX5808 SEL: GPIO%d\n", config->rx5808_sel_pin);
        Serial.printf("  Mode Switch: GPIO%d\n", config->mode_switch_pin);
    }
    
    return true;
}

bool ConfigLoader::saveCustomConfig(const CustomPinConfig* config) {
    if (!config) {
        return false;
    }
    
    Preferences prefs;
    
    // Open preferences in read-write mode
    if (!prefs.begin(NVS_NAMESPACE, false)) {
        Serial.println("ConfigLoader: Failed to open NVS for writing - cannot save config");
        return false;
    }
    
    // Save enabled flag
    prefs.putUChar("pin_enabled", config->enabled ? 1 : 0);
    
    if (config->enabled) {
        // Core pins (always save)
        prefs.putUChar("pin_rssi_input", config->rssi_input_pin);
        prefs.putUChar("pin_rx5808_data", config->rx5808_data_pin);
        prefs.putUChar("pin_rx5808_clk", config->rx5808_clk_pin);
        prefs.putUChar("pin_rx5808_sel", config->rx5808_sel_pin);
        prefs.putUChar("pin_mode_switch", config->mode_switch_pin);
        
        // Optional pins (only save if > 0)
        if (config->power_button_pin > 0) {
            prefs.putUChar("pin_power_button", config->power_button_pin);
        } else {
            prefs.remove("pin_power_button");
        }
        if (config->battery_adc_pin > 0) {
            prefs.putUChar("pin_battery_adc", config->battery_adc_pin);
        } else {
            prefs.remove("pin_battery_adc");
        }
        if (config->audio_dac_pin > 0) {
            prefs.putUChar("pin_audio_dac", config->audio_dac_pin);
        } else {
            prefs.remove("pin_audio_dac");
        }
        if (config->usb_detect_pin > 0) {
            prefs.putUChar("pin_usb_detect", config->usb_detect_pin);
        } else {
            prefs.remove("pin_usb_detect");
        }
        
        // LCD/Touch pins (save if >= 0, remove if -1)
        if (config->lcd_i2c_sda >= 0) {
            prefs.putChar("pin_lcd_i2c_sda", config->lcd_i2c_sda);
        } else {
            prefs.remove("pin_lcd_i2c_sda");
        }
        if (config->lcd_i2c_scl >= 0) {
            prefs.putChar("pin_lcd_i2c_scl", config->lcd_i2c_scl);
        } else {
            prefs.remove("pin_lcd_i2c_scl");
        }
        if (config->lcd_backlight >= 0) {
            prefs.putChar("pin_lcd_backlight", config->lcd_backlight);
        } else {
            prefs.remove("pin_lcd_backlight");
        }
    }
    
    prefs.end();
    Serial.println("ConfigLoader: Configuration saved to NVS successfully");
    return true;
}

bool ConfigLoader::hasCustomConfig() {
    Preferences prefs;
    
    // Open in read-write mode to avoid error if namespace doesn't exist
    // (read-only mode errors when namespace doesn't exist)
    if (!prefs.begin(NVS_NAMESPACE, false)) {
        return false;
    }
    
    uint8_t enabled = prefs.getUChar("pin_enabled", 0);
    prefs.end();
    
    return (enabled != 0);
}

