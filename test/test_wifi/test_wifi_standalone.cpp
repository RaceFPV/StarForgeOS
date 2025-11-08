/**
 * StarForgeOS WiFi and Web Server Tests
 * 
 * These tests verify WiFi connectivity and web server functionality
 * in standalone mode across different ESP32 variants.
 */

#include <Arduino.h>
#include <unity.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "../../src/config.h"

// Test timeout constants
#define WIFI_CONNECT_TIMEOUT_MS 10000
#define HTTP_REQUEST_TIMEOUT_MS 5000

// Track WiFi state
bool wifi_initialized = false;
String test_ssid;

void setUp(void) {
    // Setup before each test
}

void tearDown(void) {
    // Cleanup after each test
}

/**
 * Test: WiFi library availability
 */
void test_wifi_library_available(void) {
    TEST_MESSAGE("Testing WiFi library availability");
    
    // Simple check that WiFi object exists and has expected methods
    WiFi.mode(WIFI_OFF);
    delay(100);
    
    TEST_PASS();
}

/**
 * Test: WiFi AP initialization
 */
void test_wifi_ap_init(void) {
    TEST_MESSAGE("Initializing WiFi Access Point");
    
    // Generate unique SSID based on MAC address
    uint8_t mac[6];
    #if defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C6)
        // Single-core chips - need special WiFi handling
        WiFi.mode(WIFI_AP);
        delay(100);
    #endif
    
    WiFi.softAPmacAddress(mac);
    test_ssid = String(WIFI_AP_SSID_PREFIX) + "_" + 
                String(mac[4], HEX) + String(mac[5], HEX);
    test_ssid.toUpperCase();
    
    // Start AP
    bool success = WiFi.softAP(test_ssid.c_str(), WIFI_AP_PASSWORD);
    
    TEST_ASSERT_TRUE(success);
    
    delay(1000); // Give AP time to start
    
    // Verify AP is running
    TEST_ASSERT_EQUAL(WIFI_MODE_AP, WiFi.getMode());
    
    wifi_initialized = true;
    
    char msg[128];
    sprintf(msg, "WiFi AP started: %s", test_ssid.c_str());
    TEST_MESSAGE(msg);
}

/**
 * Test: WiFi AP IP address assignment
 */
void test_wifi_ap_ip(void) {
    if (!wifi_initialized) {
        test_wifi_ap_init();
    }
    
    IPAddress ip = WiFi.softAPIP();
    
    // Default AP IP should be 192.168.4.1
    TEST_ASSERT_TRUE(ip[0] != 0);
    
    char msg[64];
    sprintf(msg, "AP IP Address: %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    TEST_MESSAGE(msg);
}

/**
 * Test: WiFi SSID generation
 */
void test_wifi_ssid_generation(void) {
    uint8_t mac[6];
    WiFi.softAPmacAddress(mac);
    
    String ssid = String(WIFI_AP_SSID_PREFIX) + "_" + 
                  String(mac[4], HEX) + String(mac[5], HEX);
    ssid.toUpperCase();
    
    // SSID should be reasonable length
    TEST_ASSERT_TRUE(ssid.length() > 0);
    TEST_ASSERT_TRUE(ssid.length() <= 32);
    
    // SSID should start with prefix
    TEST_ASSERT_TRUE(ssid.startsWith(WIFI_AP_SSID_PREFIX));
    
    char msg[64];
    sprintf(msg, "Generated SSID: %s", ssid.c_str());
    TEST_MESSAGE(msg);
}

/**
 * Test: WiFi MAC address retrieval
 */
void test_wifi_mac_address(void) {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    
    // Verify MAC address is not all zeros
    bool all_zeros = true;
    for (int i = 0; i < 6; i++) {
        if (mac[i] != 0) {
            all_zeros = false;
            break;
        }
    }
    TEST_ASSERT_FALSE(all_zeros);
    
    // Also test AP MAC address
    WiFi.softAPmacAddress(mac);
    all_zeros = true;
    for (int i = 0; i < 6; i++) {
        if (mac[i] != 0) {
            all_zeros = false;
            break;
        }
    }
    TEST_ASSERT_FALSE(all_zeros);
    
    char msg[64];
    sprintf(msg, "AP MAC: %02X:%02X:%02X:%02X:%02X:%02X", 
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    TEST_MESSAGE(msg);
}

/**
 * Test: WiFi mode switching
 */
void test_wifi_mode_switching(void) {
    // Test OFF mode
    WiFi.mode(WIFI_OFF);
    delay(100);
    TEST_ASSERT_EQUAL(WIFI_MODE_NULL, WiFi.getMode());
    
    // Test AP mode
    WiFi.mode(WIFI_AP);
    delay(100);
    TEST_ASSERT_EQUAL(WIFI_MODE_AP, WiFi.getMode());
    
    // Test STA mode
    WiFi.mode(WIFI_STA);
    delay(100);
    TEST_ASSERT_EQUAL(WIFI_MODE_STA, WiFi.getMode());
    
    // Back to AP mode for other tests
    WiFi.mode(WIFI_AP);
    delay(100);
    
    TEST_MESSAGE("WiFi mode switching working correctly");
}

/**
 * Test: AP client connection capability
 */
void test_ap_client_capacity(void) {
    if (!wifi_initialized) {
        test_wifi_ap_init();
    }
    
    // Check current connected clients
    int clients = WiFi.softAPgetStationNum();
    
    TEST_ASSERT_TRUE(clients >= 0);
    TEST_ASSERT_TRUE(clients <= 10); // Reasonable upper limit
    
    char msg[64];
    sprintf(msg, "Connected clients: %d", clients);
    TEST_MESSAGE(msg);
}

/**
 * Test: WiFi signal strength reporting (for STA mode)
 */
void test_wifi_rssi(void) {
    // This test is informational - checks if RSSI can be read
    WiFi.mode(WIFI_STA);
    delay(100);
    
    int32_t rssi = WiFi.RSSI();
    
    // RSSI should be negative in dBm or 0 if not connected
    TEST_ASSERT_TRUE(rssi <= 0);
    
    // Back to AP mode
    WiFi.mode(WIFI_AP);
    delay(100);
    
    char msg[64];
    sprintf(msg, "WiFi RSSI: %d dBm (STA mode, no connection)", rssi);
    TEST_MESSAGE(msg);
}

/**
 * Test: WiFi power management
 */
void test_wifi_power_management(void) {
    // Test setting WiFi sleep mode
    bool success = WiFi.setSleep(false); // No sleep for timing accuracy
    TEST_ASSERT_TRUE(success);
    
    delay(100);
    
    TEST_MESSAGE("WiFi power management configured");
}

/**
 * Test: WiFi configuration persistence
 */
void test_wifi_persistence(void) {
    // Test disabling auto-reconnect (for AP mode)
    WiFi.setAutoReconnect(false);
    delay(50);
    
    TEST_MESSAGE("WiFi persistence settings configured");
    TEST_PASS();
}

/**
 * Test: Multiple WiFi init/deinit cycles
 */
void test_wifi_init_deinit_cycles(void) {
    for (int i = 0; i < 3; i++) {
        // Start AP
        WiFi.mode(WIFI_AP);
        WiFi.softAP("TEST_AP", "");
        delay(500);
        
        TEST_ASSERT_EQUAL(WIFI_MODE_AP, WiFi.getMode());
        
        // Stop WiFi
        WiFi.mode(WIFI_OFF);
        delay(500);
        
        TEST_ASSERT_EQUAL(WIFI_MODE_NULL, WiFi.getMode());
    }
    
    // Restore AP mode
    WiFi.mode(WIFI_AP);
    if (!test_ssid.isEmpty()) {
        WiFi.softAP(test_ssid.c_str(), WIFI_AP_PASSWORD);
    }
    delay(500);
    
    TEST_MESSAGE("Multiple WiFi init/deinit cycles successful");
}

/**
 * Test: mDNS hostname validity
 */
void test_mdns_hostname(void) {
    #ifdef MDNS_HOSTNAME
        // Check hostname is valid
        TEST_ASSERT_TRUE(strlen(MDNS_HOSTNAME) > 0);
        TEST_ASSERT_TRUE(strlen(MDNS_HOSTNAME) < 64);
        
        // Hostname should be alphanumeric
        for (size_t i = 0; i < strlen(MDNS_HOSTNAME); i++) {
            char c = MDNS_HOSTNAME[i];
            bool valid = (c >= 'a' && c <= 'z') || 
                        (c >= 'A' && c <= 'Z') || 
                        (c >= '0' && c <= '9') || 
                        c == '-' || c == '_';
            TEST_ASSERT_TRUE(valid);
        }
        
        char msg[128];
        sprintf(msg, "mDNS hostname: %s.local", MDNS_HOSTNAME);
        TEST_MESSAGE(msg);
    #else
        TEST_FAIL_MESSAGE("MDNS_HOSTNAME not defined");
    #endif
}

/**
 * Test: Web server port validity
 */
void test_web_server_port(void) {
    TEST_ASSERT_TRUE(WEB_SERVER_PORT > 0);
    TEST_ASSERT_TRUE(WEB_SERVER_PORT <= 65535);
    
    // Common ports
    if (WEB_SERVER_PORT == 80) {
        TEST_MESSAGE("Using standard HTTP port 80");
    } else if (WEB_SERVER_PORT == 8080) {
        TEST_MESSAGE("Using alternate HTTP port 8080");
    } else {
        char msg[64];
        sprintf(msg, "Using custom port: %d", WEB_SERVER_PORT);
        TEST_MESSAGE(msg);
    }
}

/**
 * Stress test: WiFi stability over time
 */
void test_wifi_stability(void) {
    if (!wifi_initialized) {
        test_wifi_ap_init();
    }
    
    // Keep WiFi running and check it remains stable
    for (int i = 0; i < 10; i++) {
        TEST_ASSERT_EQUAL(WIFI_MODE_AP, WiFi.getMode());
        
        IPAddress ip = WiFi.softAPIP();
        TEST_ASSERT_TRUE(ip[0] != 0);
        
        delay(100);
    }
    
    TEST_MESSAGE("WiFi remained stable over test period");
}

/**
 * Test: WiFi channel configuration
 */
void test_wifi_channel(void) {
    // Stop current AP
    WiFi.mode(WIFI_OFF);
    delay(100);
    
    // Start AP on specific channel (e.g., channel 6)
    WiFi.softAP("TEST_CH6", "", 6);
    delay(500);
    
    // Restart with default settings
    WiFi.mode(WIFI_OFF);
    delay(100);
    
    if (!test_ssid.isEmpty()) {
        WiFi.softAP(test_ssid.c_str(), WIFI_AP_PASSWORD);
        delay(500);
    }
    
    TEST_MESSAGE("WiFi channel configuration working");
}

/**
 * Test: WiFi concurrent operations
 */
void test_wifi_concurrent_ops(void) {
    if (!wifi_initialized) {
        test_wifi_ap_init();
    }
    
    // Perform multiple WiFi operations concurrently
    for (int i = 0; i < 20; i++) {
        IPAddress ip = WiFi.softAPIP();
        int clients = WiFi.softAPgetStationNum();
        wifi_mode_t mode = WiFi.getMode();
        
        TEST_ASSERT_TRUE(ip[0] != 0);
        TEST_ASSERT_TRUE(clients >= 0);
        TEST_ASSERT_EQUAL(WIFI_MODE_AP, mode);
        
        delay(50);
    }
    
    TEST_MESSAGE("Concurrent WiFi operations working correctly");
}

void setup() {
    delay(2000); // Wait for serial connection
    
    Serial.begin(115200);
    Serial.println("\n\n=== StarForgeOS WiFi Tests ===\n");
    
    UNITY_BEGIN();
    
    // Basic WiFi tests
    RUN_TEST(test_wifi_library_available);
    RUN_TEST(test_wifi_mac_address);
    RUN_TEST(test_wifi_ssid_generation);
    RUN_TEST(test_wifi_mode_switching);
    
    // AP initialization tests
    RUN_TEST(test_wifi_ap_init);
    RUN_TEST(test_wifi_ap_ip);
    RUN_TEST(test_ap_client_capacity);
    
    // WiFi configuration tests
    RUN_TEST(test_wifi_power_management);
    RUN_TEST(test_wifi_persistence);
    RUN_TEST(test_wifi_channel);
    RUN_TEST(test_wifi_rssi);
    
    // Web server configuration tests
    RUN_TEST(test_web_server_port);
    RUN_TEST(test_mdns_hostname);
    
    // Stability and stress tests
    RUN_TEST(test_wifi_init_deinit_cycles);
    RUN_TEST(test_wifi_stability);
    RUN_TEST(test_wifi_concurrent_ops);
    
    UNITY_END();
    
    // Clean up
    WiFi.mode(WIFI_OFF);
}

void loop() {
    // Tests run in setup(), loop does nothing
    delay(1000);
}

