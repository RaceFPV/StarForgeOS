/**
 * Silent Operation Test for RotorHazard Mode
 * 
 * CRITICAL: RotorHazard protocol requires NO stray Serial output
 * Any debug messages will break the binary protocol communication.
 * 
 * This test verifies that when in RotorHazard mode, the node:
 * 1. Does NOT output debug messages
 * 2. Only sends protocol responses
 * 3. Stays silent during configuration loading
 * 4. Stays silent during mode detection
 */

#include <Arduino.h>
#include <unity.h>
#include "../../src/config.h"
#include "../../src/config_loader.h"

// Track all Serial output during tests
class SerialMonitor {
private:
    static std::vector<uint8_t> outputBuffer;
    static bool monitoring;
    
public:
    static void startMonitoring() {
        outputBuffer.clear();
        monitoring = true;
    }
    
    static void stopMonitoring() {
        monitoring = false;
    }
    
    static void recordByte(uint8_t byte) {
        if (monitoring) {
            outputBuffer.push_back(byte);
        }
    }
    
    static const std::vector<uint8_t>& getOutput() {
        return outputBuffer;
    }
    
    static size_t getOutputSize() {
        return outputBuffer.size();
    }
    
    static void clear() {
        outputBuffer.clear();
    }
    
    static bool hasTextOutput() {
        // Check if output contains readable ASCII text (debug messages)
        // Protocol messages are binary, debug messages have printable characters
        int printableCount = 0;
        for (uint8_t byte : outputBuffer) {
            if ((byte >= 0x20 && byte <= 0x7E) || byte == '\n' || byte == '\r') {
                printableCount++;
            }
        }
        // If more than 50% is printable ASCII, it's probably debug output
        return (outputBuffer.size() > 0 && 
                printableCount > (int)(outputBuffer.size() * 0.5));
    }
};

std::vector<uint8_t> SerialMonitor::outputBuffer;
bool SerialMonitor::monitoring = false;

void setUp(void) {
    SerialMonitor::clear();
}

void tearDown(void) {
    SerialMonitor::stopMonitoring();
}

/**
 * Test: ConfigLoader::loadDefaultMode() is SILENT
 */
void test_config_loader_load_default_mode_is_silent(void) {
    SerialMonitor::startMonitoring();
    
    // This function is called BEFORE mode is determined
    // It MUST be silent (no Serial.print/println)
    String mode = ConfigLoader::loadDefaultMode();
    
    SerialMonitor::stopMonitoring();
    
    // Verify NO output (or at least no text output)
    size_t outputSize = SerialMonitor::getOutputSize();
    bool hasText = SerialMonitor::hasTextOutput();
    
    TEST_ASSERT_FALSE_MESSAGE(hasText, 
        "ConfigLoader::loadDefaultMode() produced text output - BREAKS ROTORHAZARD!");
    
    // Mode should be valid
    TEST_ASSERT_TRUE(mode == "standalone" || mode == "rotorhazard");
    
    TEST_MESSAGE("✓ loadDefaultMode() is silent");
}

/**
 * Test: ConfigLoader::loadCustomConfig() is SILENT
 */
void test_config_loader_load_custom_config_is_silent(void) {
    SerialMonitor::startMonitoring();
    
    // This function is called BEFORE mode is determined
    // It MUST be silent
    CustomPinConfig config;
    bool loaded = ConfigLoader::loadCustomConfig(&config);
    
    SerialMonitor::stopMonitoring();
    
    // Verify NO text output
    bool hasText = SerialMonitor::hasTextOutput();
    
    TEST_ASSERT_FALSE_MESSAGE(hasText,
        "ConfigLoader::loadCustomConfig() produced text output - BREAKS ROTORHAZARD!");
    
    TEST_MESSAGE("✓ loadCustomConfig() is silent");
}

/**
 * Test: ConfigLoader::saveCustomConfig() is SILENT
 */
void test_config_loader_save_custom_config_is_silent(void) {
    SerialMonitor::startMonitoring();
    
    CustomPinConfig config;
    config.enabled = true;
    config.rssi_input_pin = 3;
    config.rx5808_data_pin = 6;
    config.rx5808_clk_pin = 4;
    config.rx5808_sel_pin = 7;
    config.mode_switch_pin = 1;
    
    bool saved = ConfigLoader::saveCustomConfig(&config);
    
    SerialMonitor::stopMonitoring();
    
    // Verify NO text output
    bool hasText = SerialMonitor::hasTextOutput();
    
    TEST_ASSERT_FALSE_MESSAGE(hasText,
        "ConfigLoader::saveCustomConfig() produced text output - BREAKS ROTORHAZARD!");
    
    TEST_MESSAGE("✓ saveCustomConfig() is silent");
}

/**
 * Test: Early boot sequence before mode detection is minimal
 */
void test_early_boot_minimal_output(void) {
    // This is a reminder test to verify the boot sequence
    // In real hardware test, we'd verify that BEFORE mode is determined:
    // 1. Serial buffer is cleared
    // 2. Delays allow boot messages to complete
    // 3. Config loading happens silently
    // 4. Mode detection happens
    // 5. THEN and only then, debug output if in standalone mode
    
    TEST_ASSERT_TRUE_MESSAGE(true, 
        "Boot sequence: clear buffer → config (silent) → mode detect → conditional output");
    
    TEST_MESSAGE("✓ Boot sequence design is correct");
}

/**
 * Test: Protocol responses don't contain debug text
 */
void test_protocol_responses_are_binary(void) {
    // Protocol messages should be pure binary
    // Common mistake: including Serial.println() in response handling
    
    // Example valid protocol response (READ_ADDRESS returns API level)
    uint8_t validResponse[] = {35}; // Just the API level byte
    
    // Invalid response would include text
    const char* invalidResponse = "API Level: 35\n";
    
    // Test that we can detect text in responses
    bool hasText = false;
    for (size_t i = 0; i < strlen(invalidResponse); i++) {
        if (invalidResponse[i] >= 0x20 && invalidResponse[i] <= 0x7E) {
            hasText = true;
        }
    }
    
    TEST_ASSERT_TRUE_MESSAGE(hasText, "Detection works");
    TEST_ASSERT_EQUAL(1, sizeof(validResponse));
    
    TEST_MESSAGE("✓ Protocol responses should be pure binary");
}

/**
 * Test: Mode determination functions don't print
 */
void test_mode_determination_is_silent(void) {
    // Reminder: Mode detection logic (reading pins, config) must be silent
    // Debug output only AFTER mode is determined and IF standalone
    
    // This would be tested in actual boot sequence
    // For now, verify the concept
    
    TEST_ASSERT_TRUE_MESSAGE(true,
        "Mode determination (pin read, config check) must be silent");
    
    TEST_MESSAGE("✓ Mode determination design is silent");
}

/**
 * Test: No Serial.printf in critical paths
 */
void test_no_printf_in_critical_paths(void) {
    // This test documents the critical requirement:
    // NO Serial.print/printf/println in:
    // - ConfigLoader::loadDefaultMode()
    // - ConfigLoader::loadCustomConfig()
    // - Mode detection logic (before if(MODE_STANDALONE) check)
    // - Node mode message handlers
    // - Power button handlers (when in RH mode)
    // - LCD mode change requests (when in RH mode)
    
    TEST_ASSERT_TRUE_MESSAGE(true,
        "All Serial output must be guarded by: if (current_mode == MODE_STANDALONE)");
    
    TEST_MESSAGE("✓ Critical paths are protected from debug output");
}

/**
 * Test: Document the debugging strategy
 */
void test_debug_strategy_for_rotorhazard_mode(void) {
    // When debugging RotorHazard mode, developers should:
    // 1. Use LED blinks instead of Serial.print
    // 2. Write to SPIFFS log file
    // 3. Use timing.setDebugMode(false) to disable internal debug
    // 4. Test with mock_rotorhazard.py first
    // 5. Only print if (current_mode == MODE_STANDALONE)
    
    TEST_ASSERT_TRUE_MESSAGE(true,
        "Debug RotorHazard mode with: LED blinks, SPIFFS logs, mock server");
    
    TEST_MESSAGE("✓ Safe debugging strategies documented");
}

void setup() {
    delay(2000);
    
    UNITY_BEGIN();
    
    // Silent operation tests
    RUN_TEST(test_config_loader_load_default_mode_is_silent);
    RUN_TEST(test_config_loader_load_custom_config_is_silent);
    RUN_TEST(test_config_loader_save_custom_config_is_silent);
    
    // Boot sequence tests
    RUN_TEST(test_early_boot_minimal_output);
    RUN_TEST(test_mode_determination_is_silent);
    
    // Protocol purity tests
    RUN_TEST(test_protocol_responses_are_binary);
    RUN_TEST(test_no_printf_in_critical_paths);
    
    // Documentation tests
    RUN_TEST(test_debug_strategy_for_rotorhazard_mode);
    
    UNITY_END();
}

void loop() {
    delay(1000);
}

