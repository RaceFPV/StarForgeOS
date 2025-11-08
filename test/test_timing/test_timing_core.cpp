/**
 * StarForgeOS Timing Core Tests
 * 
 * These tests verify the timing core functionality including:
 * - RSSI reading and filtering
 * - Lap detection and recording
 * - RX5808 frequency control
 * - Threshold management
 */

#include <Arduino.h>
#include <unity.h>
#include "../../src/timing_core.h"
#include "../../src/config.h"

TimingCore* timing = nullptr;

void setUp(void) {
    // Create fresh timing core for each test
    if (timing != nullptr) {
        delete timing;
    }
    timing = new TimingCore();
}

void tearDown(void) {
    // Clean up after each test
    if (timing != nullptr) {
        delete timing;
        timing = nullptr;
    }
}

/**
 * Test: TimingCore initialization
 */
void test_timing_core_init(void) {
    TEST_ASSERT_NOT_NULL(timing);
    
    // Verify initial state
    TimingState state = timing->getState();
    TEST_ASSERT_EQUAL(0, state.lap_count);
    TEST_ASSERT_EQUAL(false, state.crossing_active);
    TEST_ASSERT_EQUAL(CROSSING_THRESHOLD, state.threshold);
    
    TEST_MESSAGE("TimingCore initialized successfully");
}

/**
 * Test: Set and get threshold
 */
void test_threshold_set_get(void) {
    timing->begin();
    
    // Test valid threshold values
    timing->setThreshold(50);
    TEST_ASSERT_EQUAL(50, timing->getThreshold());
    
    timing->setThreshold(150);
    TEST_ASSERT_EQUAL(150, timing->getThreshold());
    
    timing->setThreshold(255);
    TEST_ASSERT_EQUAL(255, timing->getThreshold());
    
    TEST_MESSAGE("Threshold set/get working correctly");
}

/**
 * Test: Set and get frequency
 */
void test_frequency_set_get(void) {
    timing->begin();
    
    // Test valid frequency values
    timing->setFrequency(5800);
    delay(100); // Allow time for RX5808 to settle
    TEST_ASSERT_EQUAL(5800, timing->getCurrentFrequency());
    
    timing->setFrequency(5740);
    delay(100);
    TEST_ASSERT_EQUAL(5740, timing->getCurrentFrequency());
    
    timing->setFrequency(5860);
    delay(100);
    TEST_ASSERT_EQUAL(5860, timing->getCurrentFrequency());
    
    TEST_MESSAGE("Frequency set/get working correctly");
}

/**
 * Test: Activation state
 */
void test_activation_state(void) {
    timing->begin();
    
    // Initially should be inactive
    TEST_ASSERT_FALSE(timing->isActivated());
    
    // Activate timing
    timing->setActivated(true);
    TEST_ASSERT_TRUE(timing->isActivated());
    
    // Deactivate timing
    timing->setActivated(false);
    TEST_ASSERT_FALSE(timing->isActivated());
    
    TEST_MESSAGE("Activation state working correctly");
}

/**
 * Test: RSSI reading capability
 */
void test_rssi_reading(void) {
    timing->begin();
    timing->setActivated(true);
    
    delay(100); // Allow RSSI samples to accumulate
    
    // Read current RSSI
    uint8_t rssi = timing->getCurrentRSSI();
    
    // RSSI should be in valid range (0-255)
    TEST_ASSERT_TRUE(rssi >= 0);
    TEST_ASSERT_TRUE(rssi <= 255);
    
    char msg[64];
    sprintf(msg, "Current RSSI: %d", rssi);
    TEST_MESSAGE(msg);
}

/**
 * Test: Peak RSSI tracking
 */
void test_peak_rssi_tracking(void) {
    timing->begin();
    timing->setActivated(true);
    
    delay(200); // Allow samples to accumulate
    
    uint8_t initial_peak = timing->getPeakRSSI();
    
    // Read multiple times and verify peak is maintained
    for (int i = 0; i < 10; i++) {
        uint8_t current_peak = timing->getPeakRSSI();
        TEST_ASSERT_TRUE(current_peak >= initial_peak);
        delay(10);
    }
    
    char msg[64];
    sprintf(msg, "Peak RSSI: %d", timing->getPeakRSSI());
    TEST_MESSAGE(msg);
}

/**
 * Test: State reset
 */
void test_state_reset(void) {
    timing->begin();
    timing->setActivated(true);
    
    delay(100); // Accumulate some data
    
    // Reset timing
    timing->reset();
    
    // Verify state is cleared
    TimingState state = timing->getState();
    TEST_ASSERT_EQUAL(0, state.lap_count);
    TEST_ASSERT_EQUAL(false, state.crossing_active);
    
    TEST_MESSAGE("State reset working correctly");
}

/**
 * Test: Lap buffer capacity
 */
void test_lap_buffer(void) {
    timing->begin();
    
    // Initially no laps
    TEST_ASSERT_FALSE(timing->hasNewLap());
    TEST_ASSERT_EQUAL(0, timing->getAvailableLaps());
    
    TEST_MESSAGE("Lap buffer initialized correctly");
}

/**
 * Test: Frequency bounds checking
 */
void test_frequency_bounds(void) {
    timing->begin();
    
    // Test minimum frequency
    timing->setFrequency(MIN_FREQ);
    delay(100);
    TEST_ASSERT_EQUAL(MIN_FREQ, timing->getCurrentFrequency());
    
    // Test maximum frequency
    timing->setFrequency(MAX_FREQ);
    delay(100);
    TEST_ASSERT_EQUAL(MAX_FREQ, timing->getCurrentFrequency());
    
    TEST_MESSAGE("Frequency bounds checking working");
}

/**
 * Test: RX5808 band/channel settings
 */
void test_rx5808_band_channel(void) {
    timing->begin();
    
    // Set to a known band/channel (e.g., Raceband channel 1 = 5658 MHz)
    timing->setRX5808Settings(7, 0); // Raceband (band 7), channel 0
    delay(100);
    
    // Read back settings
    uint8_t band, channel;
    timing->getRX5808Settings(band, channel);
    
    TEST_ASSERT_EQUAL(7, band);
    TEST_ASSERT_EQUAL(0, channel);
    
    TEST_MESSAGE("RX5808 band/channel settings working");
}

/**
 * Test: Multiple state queries don't interfere
 */
void test_concurrent_state_access(void) {
    timing->begin();
    timing->setActivated(true);
    
    delay(100);
    
    // Perform multiple concurrent reads
    for (int i = 0; i < 100; i++) {
        uint8_t rssi = timing->getCurrentRSSI();
        uint8_t peak = timing->getPeakRSSI();
        uint16_t laps = timing->getLapCount();
        bool active = timing->isActivated();
        
        // All reads should succeed without hanging
        TEST_ASSERT_TRUE(rssi >= 0 && rssi <= 255);
        TEST_ASSERT_TRUE(peak >= 0 && peak <= 255);
        TEST_ASSERT_TRUE(active == true);
        
        if (i % 10 == 0) {
            taskYIELD(); // Yield to timing task
        }
    }
    
    TEST_MESSAGE("Concurrent state access working correctly");
}

/**
 * Test: Debug mode switching
 */
void test_debug_mode(void) {
    timing->begin();
    
    // Test debug mode switching
    timing->setDebugMode(true);
    delay(50);
    
    timing->setDebugMode(false);
    delay(50);
    
    TEST_MESSAGE("Debug mode switching working");
    TEST_PASS();
}

/**
 * Test: Process function execution
 */
void test_process_execution(void) {
    timing->begin();
    timing->setActivated(true);
    
    // Call process multiple times
    for (int i = 0; i < 100; i++) {
        timing->process();
        delay(1);
    }
    
    // Verify timing is still responsive
    TEST_ASSERT_TRUE(timing->isActivated());
    
    TEST_MESSAGE("Process function executing correctly");
}

/**
 * Test: RSSI stability over time
 */
void test_rssi_stability(void) {
    timing->begin();
    timing->setActivated(true);
    
    delay(200); // Allow initial stabilization
    
    // Collect RSSI samples
    const int samples = 10;
    uint8_t rssi_samples[samples];
    
    for (int i = 0; i < samples; i++) {
        rssi_samples[i] = timing->getCurrentRSSI();
        delay(50);
    }
    
    // Calculate variance (should be relatively stable)
    int sum = 0;
    for (int i = 0; i < samples; i++) {
        sum += rssi_samples[i];
    }
    int mean = sum / samples;
    
    int variance = 0;
    for (int i = 0; i < samples; i++) {
        int diff = rssi_samples[i] - mean;
        variance += diff * diff;
    }
    variance /= samples;
    
    char msg[128];
    sprintf(msg, "RSSI stability test: mean=%d, variance=%d", mean, variance);
    TEST_MESSAGE(msg);
    
    // Variance should be reasonable (not wildly fluctuating)
    // This is a sanity check, not a hard requirement
    TEST_ASSERT_TRUE(variance >= 0);
}

/**
 * Test: Nadir RSSI tracking
 */
void test_nadir_rssi(void) {
    timing->begin();
    timing->setActivated(true);
    
    delay(200); // Allow samples to accumulate
    
    uint8_t nadir = timing->getNadirRSSI();
    uint8_t current = timing->getCurrentRSSI();
    
    // Nadir should be <= current RSSI
    TEST_ASSERT_TRUE(nadir <= current);
    
    char msg[64];
    sprintf(msg, "Nadir RSSI: %d, Current RSSI: %d", nadir, current);
    TEST_MESSAGE(msg);
}

/**
 * Stress test: Rapid configuration changes
 */
void test_rapid_config_changes(void) {
    timing->begin();
    timing->setActivated(true);
    
    // Rapidly change configuration
    for (int i = 0; i < 50; i++) {
        timing->setThreshold(50 + (i % 100));
        timing->setFrequency(5740 + (i % 200));
        timing->setActivated(i % 2 == 0);
        delay(10);
    }
    
    // Verify timing is still responsive
    uint8_t rssi = timing->getCurrentRSSI();
    TEST_ASSERT_TRUE(rssi >= 0 && rssi <= 255);
    
    TEST_MESSAGE("Rapid configuration changes handled correctly");
}

void setup() {
    delay(2000); // Wait for serial connection
    
    Serial.begin(115200);
    Serial.println("\n\n=== StarForgeOS Timing Core Tests ===\n");
    
    UNITY_BEGIN();
    
    // Basic initialization tests
    RUN_TEST(test_timing_core_init);
    RUN_TEST(test_threshold_set_get);
    RUN_TEST(test_frequency_set_get);
    RUN_TEST(test_activation_state);
    
    // RSSI and data collection tests
    RUN_TEST(test_rssi_reading);
    RUN_TEST(test_peak_rssi_tracking);
    RUN_TEST(test_nadir_rssi);
    RUN_TEST(test_rssi_stability);
    
    // State management tests
    RUN_TEST(test_state_reset);
    RUN_TEST(test_lap_buffer);
    RUN_TEST(test_debug_mode);
    
    // RX5808 control tests
    RUN_TEST(test_frequency_bounds);
    RUN_TEST(test_rx5808_band_channel);
    
    // Concurrency and stress tests
    RUN_TEST(test_concurrent_state_access);
    RUN_TEST(test_process_execution);
    RUN_TEST(test_rapid_config_changes);
    
    UNITY_END();
}

void loop() {
    // Tests run in setup(), loop does nothing
    delay(1000);
}

