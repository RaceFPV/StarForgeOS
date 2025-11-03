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
#elif defined(BOARD_JC2432W328C)
    // JC2432W328C - ESP32-D0WD-V3 with ST7789 LCD (240x320) and CST820 touch
    // This board has a unique pin layout for the integrated display
    // Available broken-out GPIOs: 35, 22, 21, 16, 4, 17
    #define RSSI_INPUT_PIN      35    // GPIO35 (ADC1_CH7) - RSSI input from RX5808 (input only, perfect for ADC)
    #define RX5808_DATA_PIN     21    // GPIO21 - DATA to RX5808 (repurposed from touch interrupt)
    #define RX5808_CLK_PIN      16    // GPIO16 - CLK to RX5808 (available GPIO)
    #define RX5808_SEL_PIN      17    // GPIO17 - LE (Latch Enable) to RX5808 (available GPIO)
    #define MODE_SWITCH_PIN     22    // GPIO22 - Mode selection (IGNORED on touch boards - use LCD button instead)
    #define POWER_BUTTON_PIN    22    // GPIO22 - Repurposed as power button (long press = deep sleep, wake on press)
    // BATTERY_ADC_PIN is defined later in the LCD UI section (GPIO34, repurposed from light sensor)
    #define UART_BAUD_RATE      921600  // Fast baud rate (works with most UART bridges)
#else
    // Generic ESP32 DevKit / ESP32-WROOM-32 (ESP32-D0WD-V3, NodeMCU-32S, etc)
    // Pin mapping compatible with standard ESP32 dev boards
    #define RSSI_INPUT_PIN      34    // GPIO34 (ADC1_CH6) - RSSI input from RX5808 (input only, good for ADC)
    #define RX5808_DATA_PIN     23    // GPIO23 (MOSI) - DATA to RX5808
    #define RX5808_CLK_PIN      18    // GPIO18 (SCK) - CLK to RX5808
    #define RX5808_SEL_PIN      5     // GPIO5 (CS) - LE (Latch Enable) to RX5808
    #define MODE_SWITCH_PIN     33    // GPIO33 - Mode selection switch (with internal pullup)
    #define UART_BAUD_RATE      115200  // UART bridge baud rate
#endif

// Mode selection (ESP32-C3 with pullup)
// NOTE: On touch boards (ENABLE_LCD_UI), mode is controlled via LCD button instead of physical pin
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
#define CROSSING_THRESHOLD  100    // Default RSSI threshold for crossing detection
#define MIN_LAP_TIME_MS     3000  // Minimum time between laps (3 seconds) - prevents false laps from threshold bouncing

// FreeRTOS task priorities
// Note: ESP32-D0WD (dual core) can run timing + web server concurrently
//       ESP32-C3 (single core) shares CPU time between tasks
#if defined(ARDUINO_ESP32C3_DEV) || defined(CONFIG_IDF_TARGET_ESP32C3)
    #define TIMING_PRIORITY     3     // High priority for timing (critical on single core)
    #define WEB_PRIORITY        1     // Lower priority for web server
#else
    #define TIMING_PRIORITY     2     // Timing on Core 1 (dual core has plenty of resources)
    #define WEB_PRIORITY        1     // Web server on Core 0
#endif

// DMA ADC configuration
// Note: DMA ADC uses ADC1 continuously, which conflicts with analogRead() on ADC1 pins
// For boards with battery monitoring on ADC1 pins, disable DMA and use polled mode
#if defined(BOARD_JC2432W328C)
    #define USE_DMA_ADC         0     // Disabled for battery monitoring compatibility
#else
    #define USE_DMA_ADC         1     // Enabled for best RSSI performance
#endif

#define DMA_SAMPLE_RATE     20000 // DMA ADC sample rate in Hz (20000 = 20kHz minimum for ESP32)
                                  // ESP32 valid range: 20kHz - 2MHz
                                  // Lower rate = less CPU overhead
                                  // Higher rate = better filtering, more responsive
                                  // Recommended: 20000-100000 Hz for lap timing
#define DMA_BUFFER_SIZE     256   // DMA buffer size in samples (larger = more averaging)


// WiFi configuration
#define WIFI_AP_SSID_PREFIX "SFOS"
#define WIFI_AP_PASSWORD    ""    // Open network for simplicity
#define WEB_SERVER_PORT     80
#define MDNS_HOSTNAME       "sfos"         // mDNS hostname (accessible as sfos.local)

// LCD/Touchscreen configuration
// LCD UI is only available on boards with integrated displays
#if defined(BOARD_JC2432W328C)
    #define ENABLE_LCD_UI   1     // JC2432W328C has integrated ST7789 LCD + CST820 touch
#else
    #define ENABLE_LCD_UI   0     // No LCD by default on other boards
#endif

#if ENABLE_LCD_UI
    #define LCD_PRIORITY    1     // Low priority for LCD updates (below timing & web)
    #define LCD_I2C_SDA     33    // Touch I2C SDA pin
    #define LCD_I2C_SCL     32    // Touch I2C SCL pin
    #define LCD_TOUCH_RST   25    // Touch reset pin
    #define LCD_TOUCH_INT   21    // Touch interrupt pin
    #define LCD_BACKLIGHT   27    // Backlight control pin
    
    // Audio configuration (built-in DAC amplifier on JC2432W328C)
    // NOTE: TTS libraries conflict with ADC driver (both legacy and driver_ng)
    // Pre-recorded audio would work, but simple beeps are sufficient
    #define ENABLE_AUDIO    1     // Enable audio beeps for lap detection
    #define AUDIO_DAC_PIN   26    // GPIO26 (DAC channel) - connected to built-in amplifier
    #define BEEP_DURATION_MS 100  // Beep duration in milliseconds
    
    // Battery monitoring configuration for 1S LiPo (3.0V - 4.2V)
    // Uses GPIO34 (ADC1_CH6) - originally used for light sensor, repurposed for battery
    // Note: GPIO34 is input-only, perfect for ADC use, works with WiFi active
    // Circuit: Battery+ -> 100kΩ -> GPIO34 -> 100kΩ -> GND + 100nF cap to GND (recommended)
    // With 2:1 divider: 4.2V battery -> 2.1V at ADC (safe for ESP32's 3.3V max)
    #define ENABLE_BATTERY_MONITOR  1     // Enabled for 1S LiPo with voltage divider
    #define BATTERY_ADC_PIN         34    // GPIO34 (ADC1_CH6) - light sensor pin (repurposed)
    #define BATTERY_VOLTAGE_DIVIDER 2.0   // 2:1 voltage divider (100kΩ + 100kΩ resistors)
    #define BATTERY_MIN_VOLTAGE     3.0   // 1S LiPo minimum (empty)
    #define BATTERY_MAX_VOLTAGE     4.2   // 1S LiPo maximum (full charge)
    #define BATTERY_SAMPLES         10    // Number of ADC samples to average for stability
    
    // Power button configuration (for JC2432W328C with repurposed pin 22)
    // Hardware: Connect momentary push button between GPIO22 and GND
    // The internal pullup resistor will be enabled, so button press = LOW signal
    // Long press (3 seconds) enters deep sleep mode
    // Press button again to wake from sleep (ESP32 will reset/reboot)
    #define ENABLE_POWER_BUTTON     1     // Enable power button functionality
    #define POWER_BUTTON_LONG_PRESS_MS  3000  // Hold for 3 seconds to enter deep sleep
#endif

// Data storage
#define MAX_LAPS_STORED     100   // Maximum laps to store in memory
#define MAX_PILOTS          2     // Maximum pilots in standalone mode

// Debug settings
#define DEBUG_SERIAL        0     // Enable debug output
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
