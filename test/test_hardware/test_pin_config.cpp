/**
 * StarForgeOS Hardware Configuration Tests
 * 
 * These tests verify that pin configurations are correctly defined
 * for each board type and that the pins can be properly initialized.
 */

#include <Arduino.h>
#include <unity.h>
#include "../../src/config.h"

void setUp(void) {
    // Set up before each test
}

void tearDown(void) {
    // Clean up after each test
}

/**
 * Test: Verify RSSI input pin is defined and valid
 */
void test_rssi_pin_defined(void) {
    #ifdef RSSI_INPUT_PIN
        TEST_ASSERT_TRUE(RSSI_INPUT_PIN >= 0);
        
        // Test that pin can be configured for analog input
        pinMode(RSSI_INPUT_PIN, INPUT);
        
        #ifdef ESP32
        // For ESP32, verify it's a valid ADC pin
        TEST_ASSERT_TRUE(RSSI_INPUT_PIN < 40);
        #endif
        
        TEST_MESSAGE("RSSI pin configured successfully");
    #else
        TEST_FAIL_MESSAGE("RSSI_INPUT_PIN not defined for this board");
    #endif
}

/**
 * Test: Verify RX5808 control pins are defined and unique
 */
void test_rx5808_pins_defined(void) {
    #ifdef RX5808_DATA_PIN
        TEST_ASSERT_TRUE(RX5808_DATA_PIN >= 0);
        TEST_ASSERT_TRUE(RX5808_CLK_PIN >= 0);
        TEST_ASSERT_TRUE(RX5808_SEL_PIN >= 0);
        
        // Verify pins are unique
        TEST_ASSERT_NOT_EQUAL(RX5808_DATA_PIN, RX5808_CLK_PIN);
        TEST_ASSERT_NOT_EQUAL(RX5808_DATA_PIN, RX5808_SEL_PIN);
        TEST_ASSERT_NOT_EQUAL(RX5808_CLK_PIN, RX5808_SEL_PIN);
        
        // Test that pins can be configured as outputs
        pinMode(RX5808_DATA_PIN, OUTPUT);
        pinMode(RX5808_CLK_PIN, OUTPUT);
        pinMode(RX5808_SEL_PIN, OUTPUT);
        
        // Test writing to pins
        digitalWrite(RX5808_DATA_PIN, LOW);
        digitalWrite(RX5808_CLK_PIN, LOW);
        digitalWrite(RX5808_SEL_PIN, HIGH);
        
        TEST_MESSAGE("RX5808 pins configured successfully");
    #else
        TEST_FAIL_MESSAGE("RX5808 pins not defined for this board");
    #endif
}

/**
 * Test: Verify mode switch pin (if applicable)
 */
void test_mode_switch_pin(void) {
    #if !ENABLE_LCD_UI
        // Only test on boards without LCD UI (which use physical switch)
        #ifdef MODE_SWITCH_PIN
            TEST_ASSERT_TRUE(MODE_SWITCH_PIN >= 0);
            
            pinMode(MODE_SWITCH_PIN, INPUT_PULLUP);
            
            // Read the pin state
            int state = digitalRead(MODE_SWITCH_PIN);
            TEST_ASSERT_TRUE(state == HIGH || state == LOW);
            
            TEST_MESSAGE("Mode switch pin configured successfully");
        #else
            TEST_FAIL_MESSAGE("MODE_SWITCH_PIN not defined for this board");
        #endif
    #else
        TEST_MESSAGE("Board uses LCD UI for mode selection - skipping physical pin test");
        TEST_PASS();
    #endif
}

/**
 * Test: Verify UART baud rate is sensible
 */
void test_uart_baud_rate(void) {
    #ifdef UART_BAUD_RATE
        TEST_ASSERT_TRUE(UART_BAUD_RATE >= 9600);
        TEST_ASSERT_TRUE(UART_BAUD_RATE <= 2000000);
        
        char msg[64];
        sprintf(msg, "UART baud rate: %d", UART_BAUD_RATE);
        TEST_MESSAGE(msg);
    #else
        TEST_FAIL_MESSAGE("UART_BAUD_RATE not defined");
    #endif
}

/**
 * Test: Verify timing configuration constants
 */
void test_timing_constants(void) {
    TEST_ASSERT_TRUE(TIMING_INTERVAL_MS >= 1);
    TEST_ASSERT_TRUE(TIMING_INTERVAL_MS <= 100);
    
    TEST_ASSERT_TRUE(RSSI_SAMPLES >= 1);
    TEST_ASSERT_TRUE(RSSI_SAMPLES <= 100);
    
    TEST_ASSERT_TRUE(CROSSING_THRESHOLD >= 0);
    TEST_ASSERT_TRUE(CROSSING_THRESHOLD <= 255);
    
    TEST_ASSERT_TRUE(MIN_LAP_TIME_MS >= 1000);
    TEST_ASSERT_TRUE(MIN_LAP_TIME_MS <= 60000);
    
    TEST_MESSAGE("Timing constants are valid");
}

/**
 * Test: Verify FreeRTOS priorities are appropriate
 */
void test_freertos_priorities(void) {
    #ifdef TIMING_PRIORITY
        TEST_ASSERT_TRUE(TIMING_PRIORITY >= 0);
        TEST_ASSERT_TRUE(TIMING_PRIORITY <= 25);
        
        TEST_ASSERT_TRUE(WEB_PRIORITY >= 0);
        TEST_ASSERT_TRUE(WEB_PRIORITY <= 25);
        
        // Timing should have higher or equal priority to web
        TEST_ASSERT_TRUE(TIMING_PRIORITY >= WEB_PRIORITY);
        
        char msg[64];
        sprintf(msg, "Timing priority: %d, Web priority: %d", TIMING_PRIORITY, WEB_PRIORITY);
        TEST_MESSAGE(msg);
    #else
        TEST_FAIL_MESSAGE("Task priorities not defined");
    #endif
}

/**
 * Test: Verify DMA ADC configuration
 */
void test_dma_adc_config(void) {
    #ifdef USE_DMA_ADC
        TEST_ASSERT_TRUE(USE_DMA_ADC == 0 || USE_DMA_ADC == 1);
        
        if (USE_DMA_ADC) {
            TEST_ASSERT_TRUE(DMA_SAMPLE_RATE >= 20000);
            TEST_ASSERT_TRUE(DMA_SAMPLE_RATE <= 2000000);
            
            TEST_ASSERT_TRUE(DMA_BUFFER_SIZE >= 16);
            TEST_ASSERT_TRUE(DMA_BUFFER_SIZE <= 4096);
            
            char msg[64];
            sprintf(msg, "DMA ADC enabled: %d Hz, buffer: %d", DMA_SAMPLE_RATE, DMA_BUFFER_SIZE);
            TEST_MESSAGE(msg);
        } else {
            TEST_MESSAGE("DMA ADC disabled for this board");
        }
    #else
        TEST_FAIL_MESSAGE("USE_DMA_ADC not defined");
    #endif
}

/**
 * Test: Verify WiFi configuration
 */
void test_wifi_config(void) {
    #ifdef WIFI_AP_SSID_PREFIX
        TEST_ASSERT_TRUE(strlen(WIFI_AP_SSID_PREFIX) > 0);
        TEST_ASSERT_TRUE(strlen(WIFI_AP_SSID_PREFIX) < 20);
        
        TEST_ASSERT_TRUE(WEB_SERVER_PORT > 0);
        TEST_ASSERT_TRUE(WEB_SERVER_PORT <= 65535);
        
        TEST_MESSAGE("WiFi configuration is valid");
    #else
        TEST_FAIL_MESSAGE("WiFi configuration not defined");
    #endif
}

/**
 * Test: Verify LCD configuration (if enabled)
 */
void test_lcd_config(void) {
    #if ENABLE_LCD_UI
        #ifdef LCD_I2C_SDA
            TEST_ASSERT_TRUE(LCD_I2C_SDA >= 0);
            TEST_ASSERT_TRUE(LCD_I2C_SCL >= 0);
            TEST_ASSERT_NOT_EQUAL(LCD_I2C_SDA, LCD_I2C_SCL);
            
            TEST_ASSERT_TRUE(LCD_BACKLIGHT >= 0);
            
            // Test I2C pins can be configured
            pinMode(LCD_I2C_SDA, INPUT_PULLUP);
            pinMode(LCD_I2C_SCL, INPUT_PULLUP);
            
            // Test backlight pin can be configured
            pinMode(LCD_BACKLIGHT, OUTPUT);
            digitalWrite(LCD_BACKLIGHT, HIGH);
            delay(100);
            digitalWrite(LCD_BACKLIGHT, LOW);
            
            TEST_MESSAGE("LCD configuration is valid");
        #else
            TEST_FAIL_MESSAGE("LCD pins not defined despite ENABLE_LCD_UI being set");
        #endif
    #else
        TEST_MESSAGE("LCD UI not enabled for this board - skipping LCD tests");
        TEST_PASS();
    #endif
}

/**
 * Test: Verify battery monitoring configuration (if enabled)
 */
void test_battery_config(void) {
    #if ENABLE_LCD_UI && ENABLE_BATTERY_MONITOR
        #ifdef BATTERY_ADC_PIN
            TEST_ASSERT_TRUE(BATTERY_ADC_PIN >= 0);
            
            TEST_ASSERT_TRUE(BATTERY_VOLTAGE_DIVIDER > 0);
            TEST_ASSERT_TRUE(BATTERY_VOLTAGE_DIVIDER <= 10);
            
            TEST_ASSERT_TRUE(BATTERY_MIN_VOLTAGE >= 2.5);
            TEST_ASSERT_TRUE(BATTERY_MIN_VOLTAGE <= 3.5);
            
            TEST_ASSERT_TRUE(BATTERY_MAX_VOLTAGE >= 4.0);
            TEST_ASSERT_TRUE(BATTERY_MAX_VOLTAGE <= 4.5);
            
            char msg[64];
            sprintf(msg, "Battery monitoring on GPIO%d, divider: %.1f", 
                    BATTERY_ADC_PIN, BATTERY_VOLTAGE_DIVIDER);
            TEST_MESSAGE(msg);
        #else
            TEST_FAIL_MESSAGE("Battery monitoring enabled but BATTERY_ADC_PIN not defined");
        #endif
    #else
        TEST_MESSAGE("Battery monitoring not enabled for this board");
        TEST_PASS();
    #endif
}

/**
 * Test: Verify power button configuration (if enabled)
 */
void test_power_button_config(void) {
    #if ENABLE_POWER_BUTTON
        #ifdef POWER_BUTTON_PIN
            TEST_ASSERT_TRUE(POWER_BUTTON_PIN >= 0);
            
            TEST_ASSERT_TRUE(POWER_BUTTON_LONG_PRESS_MS >= 1000);
            TEST_ASSERT_TRUE(POWER_BUTTON_LONG_PRESS_MS <= 10000);
            
            // Test power button pin can be configured with pullup
            pinMode(POWER_BUTTON_PIN, INPUT_PULLUP);
            
            // Read the pin state (should be HIGH when not pressed)
            int state = digitalRead(POWER_BUTTON_PIN);
            TEST_ASSERT_EQUAL(HIGH, state);
            
            TEST_MESSAGE("Power button configured successfully");
        #else
            TEST_FAIL_MESSAGE("Power button enabled but POWER_BUTTON_PIN not defined");
        #endif
    #else
        TEST_MESSAGE("Power button not enabled for this board");
        TEST_PASS();
    #endif
}

/**
 * Test: Verify frequency range constants
 */
void test_frequency_range(void) {
    TEST_ASSERT_TRUE(MIN_FREQ >= 5000);
    TEST_ASSERT_TRUE(MIN_FREQ <= MAX_FREQ);
    
    TEST_ASSERT_TRUE(MAX_FREQ <= 6000);
    TEST_ASSERT_TRUE(MAX_FREQ >= MIN_FREQ);
    
    TEST_ASSERT_TRUE(DEFAULT_FREQ >= MIN_FREQ);
    TEST_ASSERT_TRUE(DEFAULT_FREQ <= MAX_FREQ);
    
    char msg[64];
    sprintf(msg, "Frequency range: %d - %d MHz (default: %d)", 
            MIN_FREQ, MAX_FREQ, DEFAULT_FREQ);
    TEST_MESSAGE(msg);
}

/**
 * Test: Board identification
 */
void test_board_identification(void) {
    #if defined(TEST_ESP32_C3) || defined(ARDUINO_ESP32C3_DEV) || defined(CONFIG_IDF_TARGET_ESP32C3)
        TEST_MESSAGE("Board identified: ESP32-C3");
        TEST_ASSERT_TRUE(RSSI_INPUT_PIN == 3);
    #elif defined(TEST_ESP32_C6) || defined(CONFIG_IDF_TARGET_ESP32C6)
        TEST_MESSAGE("Board identified: ESP32-C6");
        TEST_ASSERT_TRUE(RSSI_INPUT_PIN == 0);
    #elif defined(TEST_ESP32_S3_TOUCH) || defined(BOARD_ESP32_S3_TOUCH)
        TEST_MESSAGE("Board identified: ESP32-S3-Touch (Waveshare)");
        TEST_ASSERT_TRUE(ENABLE_LCD_UI == 1);
    #elif defined(TEST_JC2432W328C) || defined(BOARD_JC2432W328C)
        TEST_MESSAGE("Board identified: JC2432W328C");
        TEST_ASSERT_TRUE(ENABLE_LCD_UI == 1);
    #elif defined(TEST_ESP32_S3)
        TEST_MESSAGE("Board identified: ESP32-S3");
    #elif defined(TEST_ESP32_S2)
        TEST_MESSAGE("Board identified: ESP32-S2");
    #elif defined(TEST_ESP32)
        TEST_MESSAGE("Board identified: ESP32 (Generic)");
        TEST_ASSERT_TRUE(RSSI_INPUT_PIN == 34);
    #else
        TEST_MESSAGE("Board identified: Unknown ESP32 variant");
    #endif
    
    TEST_PASS();
}

void setup() {
    delay(2000); // Wait for serial connection
    
    UNITY_BEGIN();
    
    // Pin configuration tests
    RUN_TEST(test_board_identification);
    RUN_TEST(test_rssi_pin_defined);
    RUN_TEST(test_rx5808_pins_defined);
    RUN_TEST(test_mode_switch_pin);
    RUN_TEST(test_uart_baud_rate);
    
    // Configuration constant tests
    RUN_TEST(test_timing_constants);
    RUN_TEST(test_freertos_priorities);
    RUN_TEST(test_dma_adc_config);
    RUN_TEST(test_wifi_config);
    RUN_TEST(test_frequency_range);
    
    // Optional feature tests
    RUN_TEST(test_lcd_config);
    RUN_TEST(test_battery_config);
    RUN_TEST(test_power_button_config);
    
    UNITY_END();
}

void loop() {
    // Tests run in setup(), loop does nothing
    delay(1000);
}

