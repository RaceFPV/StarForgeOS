#include "config_loader.h"
#include <SPIFFS.h>
#include <ArduinoJson.h>

const char* ConfigLoader::CONFIG_FILE_PATH = "/config.json";

bool ConfigLoader::loadCustomConfig(CustomPinConfig* config) {
    if (!config) {
        return false;
    }
    
    // Initialize SPIFFS if not already mounted
    if (!SPIFFS.begin(true)) {
        Serial.println("ConfigLoader: SPIFFS mount failed - using config.h defaults");
        return false;
    }
    
    // Check if config file exists
    if (!SPIFFS.exists(CONFIG_FILE_PATH)) {
        Serial.println("ConfigLoader: No custom config found - using config.h defaults");
        return false;
    }
    
    // Open config file
    File configFile = SPIFFS.open(CONFIG_FILE_PATH, "r");
    if (!configFile) {
        Serial.println("ConfigLoader: Failed to open config.json - using config.h defaults");
        return false;
    }
    
    // Parse JSON
    DynamicJsonDocument doc(1024);  // 1KB should be plenty for pin config
    DeserializationError error = deserializeJson(doc, configFile);
    configFile.close();
    
    if (error) {
        Serial.printf("ConfigLoader: JSON parse error: %s - using config.h defaults\n", error.c_str());
        return false;
    }
    
    // Check if custom pins are enabled
    JsonObject customPins = doc["custom_pins"];
    if (!customPins || !customPins["enabled"].as<bool>()) {
        Serial.println("ConfigLoader: Custom pins disabled - using config.h defaults");
        return false;
    }
    
    // Load custom pin values
    config->enabled = true;
    
    // Core pins (required)
    if (customPins.containsKey("rssi_input")) {
        config->rssi_input_pin = customPins["rssi_input"].as<uint8_t>();
    }
    if (customPins.containsKey("rx5808_data")) {
        config->rx5808_data_pin = customPins["rx5808_data"].as<uint8_t>();
    }
    if (customPins.containsKey("rx5808_clk")) {
        config->rx5808_clk_pin = customPins["rx5808_clk"].as<uint8_t>();
    }
    if (customPins.containsKey("rx5808_sel")) {
        config->rx5808_sel_pin = customPins["rx5808_sel"].as<uint8_t>();
    }
    if (customPins.containsKey("mode_switch")) {
        config->mode_switch_pin = customPins["mode_switch"].as<uint8_t>();
    }
    
    // Optional pins
    if (customPins.containsKey("power_button")) {
        config->power_button_pin = customPins["power_button"].as<uint8_t>();
    }
    if (customPins.containsKey("battery_adc")) {
        config->battery_adc_pin = customPins["battery_adc"].as<uint8_t>();
    }
    if (customPins.containsKey("audio_dac")) {
        config->audio_dac_pin = customPins["audio_dac"].as<uint8_t>();
    }
    if (customPins.containsKey("usb_detect")) {
        config->usb_detect_pin = customPins["usb_detect"].as<uint8_t>();
    }
    
    // LCD/Touch pins (advanced)
    if (customPins.containsKey("lcd_i2c_sda")) {
        config->lcd_i2c_sda = customPins["lcd_i2c_sda"].as<int8_t>();
    }
    if (customPins.containsKey("lcd_i2c_scl")) {
        config->lcd_i2c_scl = customPins["lcd_i2c_scl"].as<int8_t>();
    }
    if (customPins.containsKey("lcd_backlight")) {
        config->lcd_backlight = customPins["lcd_backlight"].as<int8_t>();
    }
    
    Serial.println("ConfigLoader: Custom pin configuration loaded successfully!");
    Serial.printf("  RSSI Input: GPIO%d\n", config->rssi_input_pin);
    Serial.printf("  RX5808 Data: GPIO%d\n", config->rx5808_data_pin);
    Serial.printf("  RX5808 CLK: GPIO%d\n", config->rx5808_clk_pin);
    Serial.printf("  RX5808 SEL: GPIO%d\n", config->rx5808_sel_pin);
    Serial.printf("  Mode Switch: GPIO%d\n", config->mode_switch_pin);
    
    return true;
}

bool ConfigLoader::saveCustomConfig(const CustomPinConfig* config) {
    if (!config) {
        return false;
    }
    
    // Initialize SPIFFS if not already mounted
    if (!SPIFFS.begin(true)) {
        Serial.println("ConfigLoader: SPIFFS mount failed - cannot save config");
        return false;
    }
    
    // Create JSON document
    DynamicJsonDocument doc(1024);
    
    JsonObject customPins = doc.createNestedObject("custom_pins");
    customPins["enabled"] = config->enabled;
    
    // Core pins
    customPins["rssi_input"] = config->rssi_input_pin;
    customPins["rx5808_data"] = config->rx5808_data_pin;
    customPins["rx5808_clk"] = config->rx5808_clk_pin;
    customPins["rx5808_sel"] = config->rx5808_sel_pin;
    customPins["mode_switch"] = config->mode_switch_pin;
    
    // Optional pins
    if (config->power_button_pin > 0) {
        customPins["power_button"] = config->power_button_pin;
    }
    if (config->battery_adc_pin > 0) {
        customPins["battery_adc"] = config->battery_adc_pin;
    }
    if (config->audio_dac_pin > 0) {
        customPins["audio_dac"] = config->audio_dac_pin;
    }
    if (config->usb_detect_pin > 0) {
        customPins["usb_detect"] = config->usb_detect_pin;
    }
    
    // LCD/Touch pins
    if (config->lcd_i2c_sda >= 0) {
        customPins["lcd_i2c_sda"] = config->lcd_i2c_sda;
    }
    if (config->lcd_i2c_scl >= 0) {
        customPins["lcd_i2c_scl"] = config->lcd_i2c_scl;
    }
    if (config->lcd_backlight >= 0) {
        customPins["lcd_backlight"] = config->lcd_backlight;
    }
    
    // Open file for writing
    File configFile = SPIFFS.open(CONFIG_FILE_PATH, "w");
    if (!configFile) {
        Serial.println("ConfigLoader: Failed to open config.json for writing");
        return false;
    }
    
    // Write JSON to file
    if (serializeJson(doc, configFile) == 0) {
        Serial.println("ConfigLoader: Failed to write config.json");
        configFile.close();
        return false;
    }
    
    configFile.close();
    Serial.println("ConfigLoader: Configuration saved successfully");
    return true;
}

bool ConfigLoader::hasCustomConfig() {
    if (!SPIFFS.begin(true)) {
        return false;
    }
    
    if (!SPIFFS.exists(CONFIG_FILE_PATH)) {
        return false;
    }
    
    File configFile = SPIFFS.open(CONFIG_FILE_PATH, "r");
    if (!configFile) {
        return false;
    }
    
    DynamicJsonDocument doc(512);  // Smaller doc for just checking enabled flag
    DeserializationError error = deserializeJson(doc, configFile);
    configFile.close();
    
    if (error) {
        return false;
    }
    
    return doc["custom_pins"]["enabled"].as<bool>();
}

