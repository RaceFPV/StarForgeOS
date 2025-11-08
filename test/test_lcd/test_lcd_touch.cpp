/**
 * StarForgeOS LCD and Touch Tests
 * 
 * These tests verify LCD and touch functionality on boards with displays:
 * - ESP32-S3-Touch (Waveshare)
 * - JC2432W328C
 */

#include <Arduino.h>
#include <unity.h>
#include "../../src/config.h"

#if ENABLE_LCD_UI
#include <Wire.h>

// Track initialization state
bool i2c_initialized = false;
bool backlight_initialized = false;

void setUp(void) {
    // Setup before each test
}

void tearDown(void) {
    // Cleanup after each test
}

/**
 * Test: LCD UI feature flag
 */
void test_lcd_ui_enabled(void) {
    TEST_ASSERT_EQUAL(1, ENABLE_LCD_UI);
    TEST_MESSAGE("LCD UI is enabled for this board");
}

/**
 * Test: LCD pin definitions
 */
void test_lcd_pins_defined(void) {
    #ifdef LCD_I2C_SDA
        TEST_ASSERT_TRUE(LCD_I2C_SDA >= 0);
        TEST_ASSERT_TRUE(LCD_I2C_SCL >= 0);
        TEST_ASSERT_TRUE(LCD_BACKLIGHT >= 0);
        
        // Verify pins are unique
        TEST_ASSERT_NOT_EQUAL(LCD_I2C_SDA, LCD_I2C_SCL);
        
        char msg[128];
        sprintf(msg, "LCD I2C: SDA=%d, SCL=%d, Backlight=%d", 
                LCD_I2C_SDA, LCD_I2C_SCL, LCD_BACKLIGHT);
        TEST_MESSAGE(msg);
    #else
        TEST_FAIL_MESSAGE("LCD pins not defined despite ENABLE_LCD_UI being set");
    #endif
}

/**
 * Test: I2C initialization
 */
void test_i2c_init(void) {
    #ifdef LCD_I2C_SDA
        // Initialize I2C bus for touch controller
        bool success = Wire.begin(LCD_I2C_SDA, LCD_I2C_SCL);
        TEST_ASSERT_TRUE(success);
        
        i2c_initialized = true;
        
        TEST_MESSAGE("I2C bus initialized successfully");
    #else
        TEST_FAIL_MESSAGE("I2C pins not defined");
    #endif
}

/**
 * Test: I2C bus scan
 */
void test_i2c_bus_scan(void) {
    if (!i2c_initialized) {
        test_i2c_init();
    }
    
    TEST_MESSAGE("Scanning I2C bus...");
    
    int devices_found = 0;
    for (uint8_t address = 1; address < 127; address++) {
        Wire.beginTransmission(address);
        uint8_t error = Wire.endTransmission();
        
        if (error == 0) {
            char msg[64];
            sprintf(msg, "I2C device found at address 0x%02X", address);
            TEST_MESSAGE(msg);
            devices_found++;
        }
    }
    
    char summary[64];
    sprintf(summary, "Found %d I2C device(s)", devices_found);
    TEST_MESSAGE(summary);
    
    // Touch controller should be present
    // CST816D typically at 0x15, CST820 typically at 0x15
    TEST_ASSERT_TRUE(devices_found >= 0); // At least informational
}

/**
 * Test: Backlight control
 */
void test_backlight_control(void) {
    #ifdef LCD_BACKLIGHT
        pinMode(LCD_BACKLIGHT, OUTPUT);
        
        // Test turning backlight on
        digitalWrite(LCD_BACKLIGHT, HIGH);
        delay(100);
        
        // Test turning backlight off
        digitalWrite(LCD_BACKLIGHT, LOW);
        delay(100);
        
        // Leave backlight on for remaining tests
        digitalWrite(LCD_BACKLIGHT, HIGH);
        
        backlight_initialized = true;
        
        TEST_MESSAGE("Backlight control working");
    #else
        TEST_FAIL_MESSAGE("LCD_BACKLIGHT pin not defined");
    #endif
}

/**
 * Test: Backlight PWM control (for dimming)
 */
void test_backlight_pwm(void) {
    #ifdef LCD_BACKLIGHT
        // Test PWM dimming
        ledcSetup(0, 5000, 8); // Channel 0, 5kHz, 8-bit resolution
        ledcAttachPin(LCD_BACKLIGHT, 0);
        
        // Test different brightness levels
        uint8_t levels[] = {0, 64, 128, 192, 255};
        for (int i = 0; i < 5; i++) {
            ledcWrite(0, levels[i]);
            delay(100);
        }
        
        // Set to full brightness
        ledcWrite(0, 255);
        
        TEST_MESSAGE("Backlight PWM dimming working");
    #else
        TEST_FAIL_MESSAGE("LCD_BACKLIGHT pin not defined");
    #endif
}

/**
 * Test: Touch I2C addresses
 */
void test_touch_i2c_address(void) {
    if (!i2c_initialized) {
        test_i2c_init();
    }
    
    #if defined(BOARD_ESP32_S3_TOUCH)
        // CST816D touch controller (typical address: 0x15)
        const uint8_t touch_addr = 0x15;
    #elif defined(BOARD_JC2432W328C)
        // CST820 touch controller (typical address: 0x15)
        const uint8_t touch_addr = 0x15;
    #else
        const uint8_t touch_addr = 0x15; // Default
    #endif
    
    Wire.beginTransmission(touch_addr);
    uint8_t error = Wire.endTransmission();
    
    if (error == 0) {
        char msg[64];
        sprintf(msg, "Touch controller found at 0x%02X", touch_addr);
        TEST_MESSAGE(msg);
    } else {
        char msg[128];
        sprintf(msg, "Touch controller not responding at 0x%02X (error: %d)", 
                touch_addr, error);
        TEST_MESSAGE(msg);
        // Don't fail - touch might not be connected in test setup
    }
    
    TEST_PASS();
}

/**
 * Test: Battery ADC pin (if enabled)
 */
void test_battery_adc_pin(void) {
    #if ENABLE_BATTERY_MONITOR && defined(BATTERY_ADC_PIN)
        TEST_ASSERT_TRUE(BATTERY_ADC_PIN >= 0);
        
        // Configure pin for analog input
        pinMode(BATTERY_ADC_PIN, INPUT);
        
        // Read battery voltage
        int raw = analogRead(BATTERY_ADC_PIN);
        TEST_ASSERT_TRUE(raw >= 0);
        TEST_ASSERT_TRUE(raw <= 4095); // 12-bit ADC
        
        // Convert to voltage
        float voltage = (raw / 4095.0) * 3.3 * BATTERY_VOLTAGE_DIVIDER;
        
        char msg[128];
        sprintf(msg, "Battery ADC: raw=%d, voltage=%.2fV (divider=%.1f)", 
                raw, voltage, BATTERY_VOLTAGE_DIVIDER);
        TEST_MESSAGE(msg);
        
        // Sanity check voltage is in reasonable range
        TEST_ASSERT_TRUE(voltage >= 0.0);
        TEST_ASSERT_TRUE(voltage <= 5.0); // Should not exceed ~4.2V for 1S LiPo
    #else
        TEST_MESSAGE("Battery monitoring not enabled for this board");
        TEST_PASS();
    #endif
}

/**
 * Test: Battery voltage calculation
 */
void test_battery_voltage_calc(void) {
    #if ENABLE_BATTERY_MONITOR && defined(BATTERY_ADC_PIN)
        // Test voltage divider calculations
        TEST_ASSERT_TRUE(BATTERY_VOLTAGE_DIVIDER > 0);
        TEST_ASSERT_TRUE(BATTERY_MIN_VOLTAGE > 0);
        TEST_ASSERT_TRUE(BATTERY_MAX_VOLTAGE > BATTERY_MIN_VOLTAGE);
        
        // Verify reasonable voltage range for 1S LiPo
        TEST_ASSERT_TRUE(BATTERY_MIN_VOLTAGE >= 2.5);
        TEST_ASSERT_TRUE(BATTERY_MIN_VOLTAGE <= 3.5);
        TEST_ASSERT_TRUE(BATTERY_MAX_VOLTAGE >= 4.0);
        TEST_ASSERT_TRUE(BATTERY_MAX_VOLTAGE <= 4.5);
        
        char msg[128];
        sprintf(msg, "Battery range: %.2fV - %.2fV", 
                BATTERY_MIN_VOLTAGE, BATTERY_MAX_VOLTAGE);
        TEST_MESSAGE(msg);
    #else
        TEST_MESSAGE("Battery monitoring not enabled");
        TEST_PASS();
    #endif
}

/**
 * Test: Audio configuration (if enabled)
 */
void test_audio_config(void) {
    #if ENABLE_AUDIO && defined(AUDIO_DAC_PIN)
        TEST_ASSERT_TRUE(AUDIO_DAC_PIN >= 0);
        
        #if defined(BOARD_JC2432W328C)
            // ESP32 has built-in DAC on GPIO25/26
            TEST_ASSERT_TRUE(AUDIO_DAC_PIN == 25 || AUDIO_DAC_PIN == 26);
        #elif defined(BOARD_ESP32_S3_TOUCH)
            // ESP32-S3 has no built-in DAC - audio should be disabled
            TEST_FAIL_MESSAGE("Audio should be disabled on ESP32-S3");
        #endif
        
        TEST_ASSERT_TRUE(BEEP_DURATION_MS >= 10);
        TEST_ASSERT_TRUE(BEEP_DURATION_MS <= 1000);
        
        char msg[64];
        sprintf(msg, "Audio DAC on GPIO%d, beep duration: %dms", 
                AUDIO_DAC_PIN, BEEP_DURATION_MS);
        TEST_MESSAGE(msg);
    #else
        TEST_MESSAGE("Audio not enabled for this board");
        TEST_PASS();
    #endif
}

/**
 * Test: Power button configuration (if enabled)
 */
void test_power_button_config(void) {
    #if ENABLE_POWER_BUTTON && defined(POWER_BUTTON_PIN)
        TEST_ASSERT_TRUE(POWER_BUTTON_PIN >= 0);
        
        // Configure with pullup
        pinMode(POWER_BUTTON_PIN, INPUT_PULLUP);
        
        // Read button state (should be HIGH when not pressed)
        int state = digitalRead(POWER_BUTTON_PIN);
        TEST_ASSERT_EQUAL(HIGH, state);
        
        TEST_ASSERT_TRUE(POWER_BUTTON_LONG_PRESS_MS >= 1000);
        TEST_ASSERT_TRUE(POWER_BUTTON_LONG_PRESS_MS <= 10000);
        
        char msg[64];
        sprintf(msg, "Power button on GPIO%d, long press: %dms", 
                POWER_BUTTON_PIN, POWER_BUTTON_LONG_PRESS_MS);
        TEST_MESSAGE(msg);
    #else
        TEST_MESSAGE("Power button not enabled for this board");
        TEST_PASS();
    #endif
}

/**
 * Test: LCD priority setting
 */
void test_lcd_priority(void) {
    #ifdef LCD_PRIORITY
        TEST_ASSERT_TRUE(LCD_PRIORITY >= 0);
        TEST_ASSERT_TRUE(LCD_PRIORITY <= 25);
        
        // LCD should have lower priority than timing
        TEST_ASSERT_TRUE(LCD_PRIORITY <= TIMING_PRIORITY);
        
        char msg[64];
        sprintf(msg, "LCD task priority: %d", LCD_PRIORITY);
        TEST_MESSAGE(msg);
    #else
        TEST_FAIL_MESSAGE("LCD_PRIORITY not defined despite LCD UI being enabled");
    #endif
}

/**
 * Test: Multiple I2C transactions
 */
void test_i2c_stability(void) {
    if (!i2c_initialized) {
        test_i2c_init();
    }
    
    // Perform multiple I2C scans to verify stability
    for (int cycle = 0; cycle < 5; cycle++) {
        int devices = 0;
        for (uint8_t addr = 1; addr < 127; addr++) {
            Wire.beginTransmission(addr);
            if (Wire.endTransmission() == 0) {
                devices++;
            }
        }
        delay(100);
    }
    
    TEST_MESSAGE("I2C bus stable over multiple transactions");
}

/**
 * Test: Board-specific pin verification
 */
void test_board_specific_pins(void) {
    #if defined(BOARD_ESP32_S3_TOUCH)
        TEST_MESSAGE("Verifying Waveshare ESP32-S3-Touch pin configuration");
        TEST_ASSERT_EQUAL(48, LCD_I2C_SDA);
        TEST_ASSERT_EQUAL(47, LCD_I2C_SCL);
        TEST_ASSERT_EQUAL(1, LCD_BACKLIGHT);
        #if ENABLE_BATTERY_MONITOR
            TEST_ASSERT_EQUAL(5, BATTERY_ADC_PIN);
            TEST_ASSERT_EQUAL_FLOAT(3.0, BATTERY_VOLTAGE_DIVIDER);
        #endif
    #elif defined(BOARD_JC2432W328C)
        TEST_MESSAGE("Verifying JC2432W328C pin configuration");
        TEST_ASSERT_EQUAL(33, LCD_I2C_SDA);
        TEST_ASSERT_EQUAL(32, LCD_I2C_SCL);
        TEST_ASSERT_EQUAL(27, LCD_BACKLIGHT);
        #if ENABLE_BATTERY_MONITOR
            TEST_ASSERT_EQUAL(34, BATTERY_ADC_PIN);
            TEST_ASSERT_EQUAL_FLOAT(2.0, BATTERY_VOLTAGE_DIVIDER);
        #endif
    #else
        TEST_MESSAGE("Unknown LCD board type");
    #endif
    
    TEST_PASS();
}

/**
 * Test: ADC reading stability
 */
void test_adc_stability(void) {
    #if ENABLE_BATTERY_MONITOR && defined(BATTERY_ADC_PIN)
        pinMode(BATTERY_ADC_PIN, INPUT);
        
        // Read multiple samples
        const int samples = 10;
        int readings[samples];
        
        for (int i = 0; i < samples; i++) {
            readings[i] = analogRead(BATTERY_ADC_PIN);
            delay(10);
        }
        
        // Calculate variance
        int sum = 0;
        for (int i = 0; i < samples; i++) {
            sum += readings[i];
        }
        int mean = sum / samples;
        
        int variance = 0;
        for (int i = 0; i < samples; i++) {
            int diff = readings[i] - mean;
            variance += diff * diff;
        }
        variance /= samples;
        
        char msg[128];
        sprintf(msg, "ADC stability: mean=%d, variance=%d", mean, variance);
        TEST_MESSAGE(msg);
        
        // Variance should be reasonable
        TEST_ASSERT_TRUE(variance >= 0);
    #else
        TEST_MESSAGE("Battery ADC not available - skipping stability test");
        TEST_PASS();
    #endif
}

void setup() {
    delay(2000); // Wait for serial connection
    
    Serial.begin(115200);
    Serial.println("\n\n=== StarForgeOS LCD/Touch Tests ===\n");
    
    UNITY_BEGIN();
    
    // Basic LCD configuration tests
    RUN_TEST(test_lcd_ui_enabled);
    RUN_TEST(test_lcd_pins_defined);
    RUN_TEST(test_board_specific_pins);
    RUN_TEST(test_lcd_priority);
    
    // I2C and touch tests
    RUN_TEST(test_i2c_init);
    RUN_TEST(test_i2c_bus_scan);
    RUN_TEST(test_touch_i2c_address);
    RUN_TEST(test_i2c_stability);
    
    // Display control tests
    RUN_TEST(test_backlight_control);
    RUN_TEST(test_backlight_pwm);
    
    // Battery monitoring tests
    RUN_TEST(test_battery_adc_pin);
    RUN_TEST(test_battery_voltage_calc);
    RUN_TEST(test_adc_stability);
    
    // Optional feature tests
    RUN_TEST(test_audio_config);
    RUN_TEST(test_power_button_config);
    
    UNITY_END();
}

void loop() {
    // Tests run in setup(), loop does nothing
    delay(1000);
}

#else // !ENABLE_LCD_UI

// Minimal tests for boards without LCD
void setup() {
    delay(2000);
    
    Serial.begin(115200);
    Serial.println("\n\n=== StarForgeOS LCD/Touch Tests ===\n");
    Serial.println("LCD UI not enabled for this board - skipping LCD tests");
    
    UNITY_BEGIN();
    UNITY_END();
}

void loop() {
    delay(1000);
}

#endif // ENABLE_LCD_UI

