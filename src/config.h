#ifndef CONFIG_H
#define CONFIG_H

// Hardware pin definitions for ESP32-C3 SuperMini (Hertz-hunter compatible)
#define RSSI_INPUT_PIN      3     // ADC1_CH3 - RSSI input from RX5808 (matches Hertz-hunter RSSI_PIN)
#define RX5808_DATA_PIN     6     // SPI MOSI to RX5808 module (matches Hertz-hunter SPI_DATA_PIN)
#define RX5808_CLK_PIN      4     // SPI SCK to RX5808 module (matches Hertz-hunter SPI_CLK_PIN)
#define RX5808_SEL_PIN      7     // SPI CS to RX5808 module (matches Hertz-hunter SPI_LE_PIN)
#define MODE_SWITCH_PIN     1     // Mode selection switch (GND=WiFi, 3.3V/Float=Node)

// Serial communication (USB CDC on ESP32-C3 SuperMini)
// ESP32-C3 SuperMini uses USB CDC for communication with PC/RotorHazard
// Note: USB CDC ignores baud rate (USB is packet-based), but we set it for compatibility
#define UART_BAUD_RATE      921600

// Mode selection (ESP32-C3 with pullup)
#define WIFI_MODE           LOW   // GND on switch pin = WiFi/Standalone mode
#define ROTORHAZARD_MODE    HIGH  // HIGH (floating/pullup) = RotorHazard node mode (default)
// Note: Floating (nothing connected) = ROTORHAZARD_MODE (default)

// RX5808 frequency constants  
#define MIN_FREQ            5645  // Minimum frequency (MHz)
#define MAX_FREQ            5945  // Maximum frequency (MHz)
#define DEFAULT_FREQ        5800  // Default frequency (MHz)

// Timing configuration
#define TIMING_INTERVAL_MS  1     // Core timing loop interval
#define RSSI_SAMPLES        50    // Number of RSSI samples to average (50ms smoothing window)
#define CROSSING_THRESHOLD  50    // Default RSSI threshold for crossing detection
#define TIMING_PRIORITY     1     // FreeRTOS task priority (same as main loop for single-core ESP32-C3)
#define WEB_PRIORITY        2     // Web server priority (medium)


// WiFi configuration
#define WIFI_AP_SSID_PREFIX "Tracer"
#define WIFI_AP_PASSWORD    ""    // Open network for simplicity
#define WEB_SERVER_PORT     80
#define MDNS_HOSTNAME       "tracer"         // mDNS hostname (accessible as tracer.local)

// Data storage
#define MAX_LAPS_STORED     100   // Maximum laps to store in memory
#define MAX_PILOTS          2     // Maximum pilots in standalone mode

// Debug settings
#define DEBUG_SERIAL        1     // Enable debug output
#define DEBUG_TIMING        0     // Enable timing debug output

#if DEBUG_SERIAL
  #define DEBUG_PRINT(x)    Serial.print(x)
  #define DEBUG_PRINTLN(x)  Serial.println(x)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
#endif

#if DEBUG_TIMING
  #define TIMING_PRINT(x)   Serial.print(x)
  #define TIMING_PRINTLN(x) Serial.println(x)
#else
  #define TIMING_PRINT(x)
  #define TIMING_PRINTLN(x)  
#endif

#endif // CONFIG_H
