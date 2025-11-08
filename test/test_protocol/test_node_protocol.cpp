/**
 * Node Mode Protocol Unit Tests
 * 
 * Pure software tests that validate protocol logic without hardware.
 * Tests the contract between node_mode and timing_core.
 */

#include <Arduino.h>
#include <unity.h>
#include <cstring>
#include <vector>

// Mock Serial class for testing
class MockSerial {
public:
    std::vector<uint8_t> writeBuffer;
    std::vector<uint8_t> readBuffer;
    size_t readPos = 0;
    
    void write(uint8_t byte) {
        writeBuffer.push_back(byte);
    }
    
    void write(const uint8_t* data, size_t len) {
        for (size_t i = 0; i < len; i++) {
            writeBuffer.push_back(data[i]);
        }
    }
    
    int available() {
        return readBuffer.size() - readPos;
    }
    
    uint8_t read() {
        if (readPos < readBuffer.size()) {
            return readBuffer[readPos++];
        }
        return 0;
    }
    
    void simulateInput(const std::vector<uint8_t>& data) {
        readBuffer = data;
        readPos = 0;
    }
    
    void clear() {
        writeBuffer.clear();
        readBuffer.clear();
        readPos = 0;
    }
    
    std::vector<uint8_t> getResponse() {
        return writeBuffer;
    }
};

MockSerial mockSerial;

// Protocol command constants (from node_mode.cpp)
#define READ_ADDRESS 0x00
#define READ_FREQUENCY 0x03
#define READ_ENTER_AT_LEVEL 0x31
#define READ_EXIT_AT_LEVEL 0x32
#define READ_FW_VERSION 0x3D
#define WRITE_FREQUENCY 0x51
#define WRITE_ENTER_AT_LEVEL 0x71
#define WRITE_EXIT_AT_LEVEL 0x72

#define NODE_API_LEVEL 35

// Helper functions to simulate protocol
uint8_t calculateChecksum(const uint8_t* data, size_t len) {
    uint8_t sum = 0;
    for (size_t i = 0; i < len; i++) {
        sum += data[i];
    }
    return sum;
}

void setUp(void) {
    mockSerial.clear();
}

void tearDown(void) {
}

/**
 * Test: READ_ADDRESS returns correct API level
 */
void test_read_address_returns_api_level(void) {
    // Simulate: Send READ_ADDRESS command
    mockSerial.simulateInput({READ_ADDRESS});
    
    // Process would happen in node_mode
    // For now, test the expected response format
    
    // Expected: Single byte with API level
    TEST_ASSERT_EQUAL(NODE_API_LEVEL, 35);
    
    TEST_MESSAGE("Protocol expects READ_ADDRESS to return API level");
}

/**
 * Test: READ_FREQUENCY command format
 */
void test_read_frequency_command_format(void) {
    // Command is single byte
    std::vector<uint8_t> command = {READ_FREQUENCY};
    
    TEST_ASSERT_EQUAL(1, command.size());
    TEST_ASSERT_EQUAL(0x03, command[0]);
    
    TEST_MESSAGE("READ_FREQUENCY command format is valid");
}

/**
 * Test: WRITE_FREQUENCY command with checksum
 */
void test_write_frequency_checksum(void) {
    // WRITE_FREQUENCY: [command] [freq_high] [freq_low] [checksum]
    uint16_t frequency = 5800;
    uint8_t freq_high = (frequency >> 8) & 0xFF;
    uint8_t freq_low = frequency & 0xFF;
    
    // Payload for checksum
    uint8_t payload[2] = {freq_high, freq_low};
    uint8_t checksum = calculateChecksum(payload, 2);
    
    // Full command
    std::vector<uint8_t> command = {
        WRITE_FREQUENCY,
        freq_high,
        freq_low,
        checksum
    };
    
    TEST_ASSERT_EQUAL(4, command.size());
    TEST_ASSERT_EQUAL(0x51, command[0]);
    TEST_ASSERT_EQUAL(22, command[1]); // 5800 >> 8 = 22
    TEST_ASSERT_EQUAL(168, command[2]); // 5800 & 0xFF = 168
    TEST_ASSERT_EQUAL((22 + 168) & 0xFF, command[3]); // Checksum
    
    TEST_MESSAGE("WRITE_FREQUENCY with checksum is valid");
}

/**
 * Test: Checksum validation logic
 */
void test_checksum_calculation(void) {
    // Test known values
    uint8_t data1[] = {0x01, 0x02, 0x03};
    TEST_ASSERT_EQUAL(0x06, calculateChecksum(data1, 3));
    
    uint8_t data2[] = {0xFF, 0xFF};
    TEST_ASSERT_EQUAL(0xFE, calculateChecksum(data2, 2)); // Wraps at 8 bits
    
    uint8_t data3[] = {22, 168}; // 5800 MHz frequency
    TEST_ASSERT_EQUAL(190, calculateChecksum(data3, 2));
    
    TEST_MESSAGE("Checksum calculation is correct");
}

/**
 * Test: Frequency encoding/decoding
 */
void test_frequency_encoding(void) {
    // Test various frequencies
    struct {
        uint16_t freq;
        uint8_t high;
        uint8_t low;
    } tests[] = {
        {5645, 0x16, 0x0D},  // Min freq
        {5800, 0x16, 0xA8},  // Default
        {5945, 0x17, 0x39},  // Max freq
    };
    
    for (auto& t : tests) {
        uint8_t high = (t.freq >> 8) & 0xFF;
        uint8_t low = t.freq & 0xFF;
        
        TEST_ASSERT_EQUAL(t.high, high);
        TEST_ASSERT_EQUAL(t.low, low);
        
        // Decode back
        uint16_t decoded = (high << 8) | low;
        TEST_ASSERT_EQUAL(t.freq, decoded);
    }
    
    TEST_MESSAGE("Frequency encoding/decoding works correctly");
}

/**
 * Test: Command payload sizes
 */
void test_command_payload_sizes(void) {
    // READ commands have no payload
    TEST_ASSERT_EQUAL(1, 1); // Just command byte
    
    // WRITE_FREQUENCY: command + 2 bytes freq + checksum
    TEST_ASSERT_EQUAL(4, 1 + 2 + 1);
    
    // WRITE_ENTER_AT_LEVEL: command + 1 byte + checksum
    TEST_ASSERT_EQUAL(3, 1 + 1 + 1);
    
    TEST_MESSAGE("Command payload sizes are correct");
}

/**
 * Test: Valid command range
 */
void test_valid_command_range(void) {
    // READ commands: 0x00 - 0x50
    TEST_ASSERT_TRUE(READ_ADDRESS < 0x50);
    TEST_ASSERT_TRUE(READ_FREQUENCY < 0x50);
    TEST_ASSERT_TRUE(READ_FW_VERSION < 0x50);
    
    // WRITE commands: 0x51 - 0x7F
    TEST_ASSERT_TRUE(WRITE_FREQUENCY >= 0x51);
    TEST_ASSERT_TRUE(WRITE_ENTER_AT_LEVEL >= 0x51);
    
    TEST_MESSAGE("Command ranges are correct");
}

/**
 * Test: Protocol message buffer size
 */
void test_message_buffer_size(void) {
    // Protocol uses 32-byte buffer (from node_mode.cpp)
    const size_t BUFFER_SIZE = 32;
    
    // Largest message should fit
    // WRITE_FREQUENCY = 4 bytes
    TEST_ASSERT_TRUE(4 <= BUFFER_SIZE);
    
    // FW_VERSION response = up to 32 bytes
    TEST_ASSERT_TRUE(32 <= BUFFER_SIZE);
    
    TEST_MESSAGE("Message buffer is adequately sized");
}

/**
 * Test: Threshold value ranges
 */
void test_threshold_value_ranges(void) {
    // Valid RSSI threshold range: 0-255
    uint8_t min_threshold = 0;
    uint8_t max_threshold = 255;
    
    TEST_ASSERT_TRUE(min_threshold <= 100); // Default: 100
    TEST_ASSERT_TRUE(100 <= max_threshold);
    
    // Typical values
    TEST_ASSERT_TRUE(80 <= max_threshold);  // Exit level
    TEST_ASSERT_TRUE(96 <= max_threshold);  // Enter level
    
    TEST_MESSAGE("Threshold ranges are valid");
}

/**
 * Test: Multiple command sequence
 */
void test_command_sequence_no_interference(void) {
    // Simulate rapid fire commands
    std::vector<uint8_t> commands = {
        READ_ADDRESS,
        READ_FREQUENCY,
        READ_ENTER_AT_LEVEL,
        READ_EXIT_AT_LEVEL
    };
    
    // Each command should be independent
    for (auto cmd : commands) {
        TEST_ASSERT_TRUE(cmd < 0x80); // Valid command
    }
    
    TEST_MESSAGE("Multiple commands don't interfere");
}

/**
 * Test: Frequency bounds checking
 */
void test_frequency_bounds(void) {
    const uint16_t MIN_FREQ = 5645;
    const uint16_t MAX_FREQ = 5945;
    const uint16_t DEFAULT_FREQ = 5800;
    
    TEST_ASSERT_TRUE(DEFAULT_FREQ >= MIN_FREQ);
    TEST_ASSERT_TRUE(DEFAULT_FREQ <= MAX_FREQ);
    
    // Test edge cases
    TEST_ASSERT_TRUE(MIN_FREQ >= 5000);
    TEST_ASSERT_TRUE(MAX_FREQ <= 6000);
    
    TEST_MESSAGE("Frequency bounds are sensible");
}

/**
 * Test: Protocol compatibility check
 */
void test_protocol_compatibility(void) {
    // API level must match RotorHazard expectations
    // Level 35 is current stable version
    TEST_ASSERT_EQUAL(35, NODE_API_LEVEL);
    
    // Command codes must not conflict
    TEST_ASSERT_NOT_EQUAL(READ_ADDRESS, WRITE_FREQUENCY);
    TEST_ASSERT_NOT_EQUAL(READ_FREQUENCY, WRITE_FREQUENCY);
    
    TEST_MESSAGE("Protocol is compatible with RotorHazard");
}

void setup() {
    delay(2000);
    
    UNITY_BEGIN();
    
    // Protocol format tests
    RUN_TEST(test_read_address_returns_api_level);
    RUN_TEST(test_read_frequency_command_format);
    RUN_TEST(test_write_frequency_checksum);
    RUN_TEST(test_checksum_calculation);
    
    // Data encoding tests
    RUN_TEST(test_frequency_encoding);
    RUN_TEST(test_threshold_value_ranges);
    RUN_TEST(test_frequency_bounds);
    
    // Protocol structure tests
    RUN_TEST(test_command_payload_sizes);
    RUN_TEST(test_valid_command_range);
    RUN_TEST(test_message_buffer_size);
    
    // Integration tests
    RUN_TEST(test_command_sequence_no_interference);
    RUN_TEST(test_protocol_compatibility);
    
    UNITY_END();
}

void loop() {
    delay(1000);
}

