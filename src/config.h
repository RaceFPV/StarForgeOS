#ifndef CONFIG_H
#define CONFIG_H

// Hardware pin definitions - board-specific
#if defined(ARDUINO_ESP32C3_DEV) || defined(CONFIG_IDF_TARGET_ESP32C3)
    // ESP32-C3 SuperMini (Hertz-hunter compatible)
    #define RSSI_INPUT_PIN      3     // GPIO3 (ADC1_CH3) - RSSI input from RX5808
    #define RX5808_DATA_PIN     6     // GPIO6 - DATA (SPI MOSI) to RX5808
    #define RX5808_CLK_PIN      4     // GPIO4 - CLK (SPI SCK) to RX5808
    #define RX5808_SEL_PIN      7     // GPIO7 - LE (Latch Enable / SPI CS) to RX5808
    #define MODE_SWITCH_PIN     1     // GPIO1 - Mode selection switch
    #define UART_BAUD_RATE      921600  // USB CDC ignores this, but set for compatibility
#else
    // Generic ESP32 DevKit / ESP32-WROOM-32 (NodeMCU-32S, Feather HUZZAH32, etc)
    #define RSSI_INPUT_PIN      39    // GPIO39 (A3/ADC1_CH3) - RSSI input from RX5808
    #define RX5808_DATA_PIN     14    // GPIO14 - DATA (MOSI) to RX5808
    #define RX5808_CLK_PIN      21    // GPIO21 - CLK (SCK) to RX5808
    #define RX5808_SEL_PIN      32    // GPIO32 - LE (Latch Enable / CS) to RX5808
    #define MODE_SWITCH_PIN     33    // GPIO33 - Mode selection switch
    #define UART_BAUD_RATE      921600  // UART bridge - use 921600 for compatibility
#endif

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
#define RSSI_SAMPLES        10    // Number of RSSI samples to average (50ms smoothing window)
#define CROSSING_THRESHOLD  50    // Default RSSI threshold for crossing detection
#define TIMING_PRIORITY     1     // FreeRTOS task priority (same as main loop for single-core ESP32-C3)
#define WEB_PRIORITY        2     // Web server priority (medium)

// DMA ADC configuration
#define USE_DMA_ADC         1     // Use DMA for ADC sampling (0 = polled, 1 = DMA)
#define DMA_SAMPLE_RATE     1000  // DMA ADC sample rate in Hz (1000 = 1kHz, 5000 = 5kHz, 10000 = 10kHz)
                                  // Lower rate = less variability, more stable readings
                                  // Higher rate = better filtering, more responsive
                                  // Recommended: 1000-5000 Hz for lap timing
#define DMA_BUFFER_SIZE     256   // DMA buffer size in samples (larger = more averaging)


// WiFi configuration
#define WIFI_AP_SSID_PREFIX "SFOS"
#define WIFI_AP_PASSWORD    ""    // Open network for simplicity
#define WEB_SERVER_PORT     80
#define MDNS_HOSTNAME       "sfos"         // mDNS hostname (accessible as sfos.local)

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
