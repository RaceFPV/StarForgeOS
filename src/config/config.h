#ifndef CONFIG_H
#define CONFIG_H

// Hardware pin definitions - board-specific
#if defined(CONFIG_IDF_TARGET_ESP32C6)
    // ESP32-C6 (WiFi 6, single-core RISC-V)
    // Note: GPIO3 is used for USB, so we use GPIO0 for ADC instead
    #define RSSI_INPUT_PIN      0     // GPIO0 (ADC1_CH0) - RSSI input from RX5808
    #define RX5808_DATA_PIN     6     // GPIO6 - DATA (SPI MOSI) to RX5808
    #define RX5808_CLK_PIN      4     // GPIO4 - CLK (SPI SCK) to RX5808
    #define RX5808_SEL_PIN      7     // GPIO7 - LE (Latch Enable / SPI CS) to RX5808
    #define MODE_SWITCH_PIN     1     // GPIO1 - Mode selection switch
    #define USE_DMA_ADC         1     // Enabled for best RSSI performance
    #define UART_BAUD_RATE      921600  // USB CDC ignores this, but set for compatibility
    #define STATUS_LED_PIN      8     // GPIO8 - Built-in status LED (if present, optional)
    #define STATUS_LED_INVERTED 0     // LED active state (0 = HIGH is ON, 1 = LOW is ON)
    #define RSSI_HISTORY_SIZE   30000  // 30,000 samples = 10 minutes @ 50Hz (150 KB) - full size for C6
#elif defined(BOARD_NUCLEARCOUNTER)
    // NuclearCounter (ESP32-C3 based board)
    #define RSSI_INPUT_PIN      3     // GPIO3 (ADC1_CH3) - RSSI input from RX5808
    #define RX5808_DATA_PIN     6     // GPIO6 - DATA (SPI MOSI) to RX5808
    #define RX5808_CLK_PIN      4     // GPIO4 - CLK (SPI SCK) to RX5808
    #define RX5808_SEL_PIN      7     // GPIO7 - LE (Latch Enable / SPI CS) to RX5808
    #define MODE_SWITCH_PIN     20    // GPIO20 - Mode selection switch
    #define USE_DMA_ADC         1     // Enabled for best RSSI performance
    #define UART_BAUD_RATE      921600  // USB CDC ignores this, but set for compatibility
    #define STATUS_LED_PIN      8     // GPIO1 - Built-in status LED (if present, optional)
    #define STATUS_LED_INVERTED 0     // LED active state (0 = HIGH is ON, 1 = LOW is ON)
    #define RSSI_HISTORY_SIZE   30000  // 30,000 samples = 10 minutes @ 50Hz (150 KB) - full size for C3
#elif defined(BOARD_ESP32_C3_ZERO)
    // ESP32-C3 Zero - Must come BEFORE generic ESP32-C3 check (more specific)
    #define RSSI_INPUT_PIN      3     // GPIO3 (ADC1_CH3) - RSSI input from RX5808
    #define RX5808_DATA_PIN     6     // GPIO6 - DATA (SPI MOSI) to RX5808
    #define RX5808_CLK_PIN      4     // GPIO4 - CLK (SPI SCK) to RX5808
    #define RX5808_SEL_PIN      7     // GPIO7 - LE (Latch Enable / SPI CS) to RX5808
    #define MODE_SWITCH_PIN     1     // GPIO1 - Mode selection switch
    #define USE_DMA_ADC         1     // Enabled for best RSSI performance
    #define UART_BAUD_RATE      921600  // USB CDC ignores this, but set for compatibility
    // WS2812 RGB LED on GPIO10 - uses built-in neopixelWrite() function
    #define STATUS_LED_PIN      10     // GPIO10 - WS2812 RGB LED
    #define STATUS_LED_WS2812   1     // Enable WS2812 support (uses built-in neopixelWrite() function)
    #define STATUS_LED_INVERTED 0     // Not used for WS2812 (color-based control)
    #define RSSI_HISTORY_SIZE   30000  // 30,000 samples = 10 minutes @ 50Hz (150 KB) - full size for C3
#elif defined(ARDUINO_ESP32C3_DEV) || defined(CONFIG_IDF_TARGET_ESP32C3)
    // ESP32-C3 SuperMini (Hertz-hunter compatible)
    // Generic ESP32-C3 - Must come AFTER specific board checks
    #define RSSI_INPUT_PIN      3     // GPIO3 (ADC1_CH3) - RSSI input from RX5808
    #define RX5808_DATA_PIN     6     // GPIO6 - DATA (SPI MOSI) to RX5808
    #define RX5808_CLK_PIN      4     // GPIO4 - CLK (SPI SCK) to RX5808
    #define RX5808_SEL_PIN      7     // GPIO7 - LE (Latch Enable / SPI CS) to RX5808
    #define MODE_SWITCH_PIN     1     // GPIO1 - Mode selection switch
    #define USE_DMA_ADC         1     // Enabled for best RSSI performance
    #define UART_BAUD_RATE      921600  // USB CDC ignores this, but set for compatibility
    #define STATUS_LED_PIN      8     // GPIO8 - Built-in status LED (if present, optional)
    #define STATUS_LED_INVERTED 0     // LED active state (0 = HIGH is ON, 1 = LOW is ON)
    #define RSSI_HISTORY_SIZE   30000  // 30,000 samples = 10 minutes @ 50Hz (150 KB) - full size for C3
#elif defined(BOARD_ESP32_S3_TOUCH)
    // Waveshare ESP32-S3-Touch-LCD-2 with 2" ST7789T3 LCD (240x320) and CST816D touch
    // Pin configuration verified from official Waveshare demo code
    // Display uses: 1 (BL), 38 (MOSI), 39 (SCLK), 40 (MISO), 42 (DC), 45 (CS)
    // Touch I2C uses: 47 (SCL), 48 (SDA)
    // Available GPIOs: 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 35, 36, 37, 41, 43, 44, 46
    #define RSSI_INPUT_PIN      4     // GPIO4 (ADC1_CH3) - RSSI input from RX5808
    #define RX5808_DATA_PIN     17    // GPIO17 - DATA to RX5808
    #define RX5808_CLK_PIN      18    // GPIO18 - CLK to RX5808
    #define RX5808_SEL_PIN      21    // GPIO21 - LE (Latch Enable) to RX5808
    #define MODE_SWITCH_PIN     0     // GPIO0 - Boot button (IGNORED - use LCD button for mode)
    #define POWER_BUTTON_PIN    0     // GPIO0 - Boot button (long press = deep sleep)
    #define USE_DMA_ADC         1     // Enabled for best RSSI performance
    #define UART_BAUD_RATE      921600  // USB CDC for serial communication
    #define RSSI_HISTORY_SIZE   10000  // 10,000 samples = ~3.3 minutes @ 50Hz (50 KB) - reduced for S3 memory constraints
#elif defined(BOARD_JC2432W328C)
    // JC2432W328C - ESP32-D0WD-V3 with ST7789 LCD (240x320) and CST820 touch
    // This board has a unique pin layout for the integrated display
    // Available broken-out GPIOs: 35, 22, 21, 16, 4, 17
    #define RSSI_INPUT_PIN      35    // GPIO35 (ADC1_CH7) - RSSI input from RX5808 (input only, perfect for ADC)
    #define RX5808_DATA_PIN     21    // GPIO21 - DATA to RX5808 (repurposed from touch interrupt)
    #define RX5808_CLK_PIN      16    // GPIO16 - CLK to RX5808 (available GPIO)
    #define RX5808_SEL_PIN      17    // GPIO17 - LE (Latch Enable) to RX5808 (available GPIO)
    #define MODE_SWITCH_PIN     22    // GPIO22 - Mode selection (IGNORED on touch boards - use LCD button instead)
    #define USB_DETECT_PIN      22    // GPIO22 - USB detection via D+ line monitoring (repurposed from mode switch)
    // BATTERY_ADC_PIN is defined later in the LCD UI section (GPIO34, repurposed from light sensor)
    #define USE_DMA_ADC         0     // Disabled for battery monitoring compatibility
    #define UART_BAUD_RATE      921600  // Fast baud rate (works with most UART bridges)
    // No status LED defined - board may have addressable LED (not yet configured)
#elif defined(BOARD_T_ENERGY)
    // LilyGo T-Energy (ESP32-S3 with built-in battery)
    // Built-in 18650 battery slot with voltage sensing circuit
    // Available GPIOs: 1-21, 35-48 (avoid 43-46 if using USB/JTAG)
    #define RSSI_INPUT_PIN      4     // GPIO4 (ADC1_CH3) - RSSI input from RX5808
    #define RX5808_DATA_PIN     5    // GPIO11 - DATA (SPI MOSI) to RX5808
    #define RX5808_CLK_PIN      7    // GPIO12 - CLK (SPI SCK) to RX5808
    #define RX5808_SEL_PIN      6    // GPIO10 - LE (Latch Enable / SPI CS) to RX5808
    #define MODE_SWITCH_PIN     9     // GPIO9 - Mode selection switch (tie to GND for WiFi mode, HIGH/floating for RotorHazard mode)
    #define USE_DMA_ADC         0     // Disabled for battery monitoring compatibility (DMA ADC conflicts with analogRead on ADC1)
    #define UART_BAUD_RATE      921600  // USB CDC for serial communication

    // Battery monitoring configuration (built-in 2:1 voltage divider)
    // T-Energy board always has battery hardware - always enabled
    #define BATTERY_ADC_PIN             3     // GPIO3 (ADC1_CH2) - Battery voltage input (schematic confirms IO03)
    #define BATTERY_VOLTAGE_DIVIDER     2.0   // Built-in 2:1 divider circuit
    #define BATTERY_ADC_CALIBRATION     1.0
    #define BATTERY_ADC_OFFSET          2     // Calibration offset (matches PhobosLT VBAT_ADD)
    #define BATTERY_MIN_VOLTAGE         3.0   // 18650 minimum (empty/cutoff)
    #define BATTERY_MAX_VOLTAGE         4.2   // 18650 maximum (full charge)
    #define BATTERY_SAMPLES             10
    #define ENABLE_BATTERY_MONITOR      1     // Always enabled for T-Energy (hardware always present)
    
    // Power button configuration
    #define ENABLE_POWER_BUTTON         0     // No power button on T-Energy board
    
    // Status LED configuration
    // T-Energy may have an addressable LED (not confirmed in schematics)
    // Using GPIO2 as default for ESP32-S3 built-in LED (if present)
    #define STATUS_LED_PIN      1     // GPIO2 - Status LED (if present, optional)
    #define STATUS_LED_INVERTED 0     // LED active state (0 = HIGH is ON, 1 = LOW is ON)
    #define RSSI_HISTORY_SIZE   10000  // 10,000 samples = ~3.3 minutes @ 50Hz (50 KB) - reduced for S3 memory constraints
#elif defined(BOARD_ESP32_S3_DEVKITC)
    // ESP32-S3-DevKitC-1 (standard development board)
    // Standard pinout for ESP32-S3-DevKitC-1
    // Available GPIOs: 1-21, 35-48 (avoid 43-46 if using USB/JTAG)
    #define RSSI_INPUT_PIN      4     // GPIO4 (ADC1_CH3) - RSSI input from RX5808
    #define RX5808_DATA_PIN     11    // GPIO11 - DATA (SPI MOSI) to RX5808
    #define RX5808_CLK_PIN      12    // GPIO12 - CLK (SPI SCK) to RX5808
    #define RX5808_SEL_PIN      10    // GPIO13 - LE (Latch Enable / SPI CS) to RX5808
    #define MODE_SWITCH_PIN     9     // GPIO9 - Mode selection switch (tie to GND for WiFi mode, HIGH/floating for RotorHazard mode)
    #define USE_DMA_ADC         1     // Enabled for best RSSI performance
    #define UART_BAUD_RATE      921600  // USB CDC for serial communication
    #define STATUS_LED_PIN      2     // GPIO2 - Built-in status LED (if present, optional)
    #define STATUS_LED_INVERTED 0     // LED active state (0 = HIGH is ON, 1 = LOW is ON)
    #define RSSI_HISTORY_SIZE   10000  // 10,000 samples = ~3.3 minutes @ 50Hz (50 KB) - reduced for S3 memory constraints
#else
    // Generic ESP32 DevKit / ESP32-WROOM-32 (ESP32-D0WD-V3, NodeMCU-32S, etc)
    // Pin mapping compatible with standard ESP32 dev boards
    #define RSSI_INPUT_PIN      34    // GPIO34 (ADC1_CH6) - RSSI input from RX5808 (input only, good for ADC)
    #define RX5808_DATA_PIN     23    // GPIO23 (MOSI) - DATA to RX5808
    #define RX5808_CLK_PIN      18    // GPIO18 (SCK) - CLK to RX5808
    #define RX5808_SEL_PIN      5     // GPIO5 (CS) - LE (Latch Enable) to RX5808
    #define MODE_SWITCH_PIN     33    // GPIO33 - Mode selection switch (with internal pullup)
    #define USE_DMA_ADC         1     // Enabled for best RSSI performance
    #define UART_BAUD_RATE      115200  // UART bridge baud rate
    // #define STATUS_LED_PIN      2     // GPIO2 - Built-in status LED (if present, optional)
    // #define STATUS_LED_INVERTED 0     // LED active state (0 = HIGH is ON, 1 = LOW is ON)
    #define RSSI_HISTORY_SIZE   30000  // 30,000 samples = 10 minutes @ 50Hz (150 KB) - full size for generic ESP32
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
#define RSSI_SAMPLES        10    // Number of RSSI samples to average (50ms smoothing window) - DEPRECATED: Using Kalman filter instead
#define ENTER_RSSI          120   // Default RSSI threshold to start capturing peak (enter threshold)
#define EXIT_RSSI           100   // Default RSSI threshold to detect lap completion (exit threshold)
#define MIN_LAP_MS          0     // Minimum lap time in milliseconds (0 = disabled, prevents peak capture until elapsed)
#define MIN_LAP_TIME_MS     3000  // Minimum time between laps (3 seconds) - prevents false laps from threshold bouncing

// Kalman filter configuration for RSSI filtering
#define RSSI_FILTER_Q       2000  // Measurement noise (scaled by 0.01 = 20.0)
#define RSSI_FILTER_R       40    // Process noise (scaled by 0.0001 = 0.004)

// FreeRTOS task priorities
// Note: ESP32-D0WD (dual core) can run timing + web server concurrently
//       ESP32-C3/C6 (single core) shares CPU time between tasks
#if defined(CONFIG_IDF_TARGET_ESP32C6) || defined(ARDUINO_ESP32C3_DEV) || defined(CONFIG_IDF_TARGET_ESP32C3)
    #define TIMING_PRIORITY     2     // High priority for timing (critical on single core)
    #define WEB_PRIORITY        1     // Lower priority for web server
#else
    #define TIMING_PRIORITY     2     // Timing on Core 1 (dual core has plenty of resources)
    #define WEB_PRIORITY        1     // Web server on Core 0
#endif

// DMA ADC configuration
// Note: DMA ADC uses ADC1 continuously, which conflicts with analogRead() on ADC1 pins
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
// Note: Can be explicitly disabled via build flag -DENABLE_LCD_UI=0
#ifndef ENABLE_LCD_UI
    #if defined(BOARD_JC2432W328C) || defined(BOARD_ESP32_S3_TOUCH)
        #define ENABLE_LCD_UI   1     // JC2432W328C and ESP32-S3-Touch have integrated displays
    #else
        #define ENABLE_LCD_UI   0     // No LCD by default on other boards
    #endif
#endif

#if ENABLE_LCD_UI
    #define LCD_PRIORITY    1     // Low priority for LCD updates (below timing & web)
    
    // ========== Board-Specific Touch LCD Configurations ==========
    #if defined(BOARD_ESP32_S3_TOUCH)
        // === Waveshare ESP32-S3-Touch-LCD-2 (2.0" ST7789T3, CST816D touch) ===
        // LCD/Touch pins
        #define LCD_I2C_SDA         48    // Touch I2C SDA pin
        #define LCD_I2C_SCL         47    // Touch I2C SCL pin
        #define LCD_TOUCH_RST       -1    // Touch reset pin (not used)
        #define LCD_TOUCH_INT       -1    // Touch interrupt pin (not used)
        #define LCD_BACKLIGHT       1     // Backlight control pin
        
        // Battery monitoring (onboard 3:1 voltage divider)
        // Note: Can be explicitly disabled via build flag -DENABLE_BATTERY_MONITOR=0
        #if !defined(ENABLE_BATTERY_MONITOR)
            #define ENABLE_BATTERY_MONITOR      1
        #endif
        #if ENABLE_BATTERY_MONITOR
            #define BATTERY_ADC_PIN             5     // GPIO5 (ADC1_CH4)
            #define BATTERY_VOLTAGE_DIVIDER     3.0   // Onboard 3:1 divider
            #define BATTERY_ADC_CALIBRATION     1.0
            #define BATTERY_MIN_VOLTAGE         3.0   // 1S LiPo minimum (empty)
            #define BATTERY_MAX_VOLTAGE         4.2   // 1S LiPo maximum (full)
            #define BATTERY_SAMPLES             10
        #endif
        
        // Audio (ESP32-S3 has no built-in DAC)
        #define ENABLE_AUDIO        0
        #define AUDIO_DAC_PIN       -1
        
        // Power button (GPIO0 boot button)
        // Note: Can be explicitly disabled via build flag -DENABLE_POWER_BUTTON=0
        #if !defined(ENABLE_POWER_BUTTON)
            #define ENABLE_POWER_BUTTON         1
        #endif
        #if ENABLE_POWER_BUTTON
            #define POWER_BUTTON_LONG_PRESS_MS  3000
        #endif
        
    #elif defined(BOARD_JC2432W328C)
        // === JC2432W328C (2.8" ST7789, CST820 touch, ESP32-D0WD-V3) ===
        // LCD/Touch pins
        #define LCD_I2C_SDA         33    // Touch I2C SDA pin
        #define LCD_I2C_SCL         32    // Touch I2C SCL pin
        #define LCD_TOUCH_RST       25    // Touch reset pin
        #define LCD_TOUCH_INT       21    // Touch interrupt pin
        #define LCD_BACKLIGHT       27    // Backlight control pin
        
        // Battery monitoring (external 2:1 voltage divider)
        // Circuit: Battery+ -> 100kΩ -> GPIO34 -> 100kΩ -> GND + 100nF cap to GND
        // Note: Can be explicitly disabled via build flag -DENABLE_BATTERY_MONITOR=0
        #if !defined(ENABLE_BATTERY_MONITOR)
            #define ENABLE_BATTERY_MONITOR      1
        #endif
        #if ENABLE_BATTERY_MONITOR
            #define BATTERY_ADC_PIN             34    // GPIO34 (ADC1_CH6, repurposed from light sensor)
            #define BATTERY_VOLTAGE_DIVIDER     2.0   // External 2:1 divider (100kΩ + 100kΩ)
            #define BATTERY_ADC_CALIBRATION     1.0
            #define BATTERY_MIN_VOLTAGE         3.0   // 1S LiPo minimum (empty)
            #define BATTERY_MAX_VOLTAGE         4.2   // 1S LiPo maximum (full)
            #define BATTERY_SAMPLES             10
        #endif
        
        // Audio (ESP32 has built-in DAC)
        #define ENABLE_AUDIO        1
        #define AUDIO_DAC_PIN       26    // GPIO26 (DAC channel, built-in amplifier)
        #define BEEP_DURATION_MS    100
        
        // Power button disabled (GPIO22 repurposed for USB detection)
        // Note: Can be explicitly enabled via build flag -DENABLE_POWER_BUTTON=1
        #if !defined(ENABLE_POWER_BUTTON)
            #define ENABLE_POWER_BUTTON  0
        #endif
        
        // USB detection via D+ line monitoring
        // Hardware: D+ breakout -> 100kΩ resistor -> GPIO22
        //           Optional: GPIO22 -> 10kΩ resistor -> GND (pull-down)
        #define USB_DETECT_SAMPLES    10  // Samples for majority-vote filtering
    #endif
    // ========== End Board-Specific Configurations ==========
#endif

// Battery monitoring configuration defaults
// Note: Battery monitoring only works in standalone mode, not in RotorHazard node mode
// If battery monitoring is enabled, ensure BATTERY_ADC_PIN is defined in board-specific config
// or via build flags (e.g., -DBATTERY_ADC_PIN=1)
// Runtime warning will be printed in standalone mode if pin is not defined
#if defined(ENABLE_BATTERY_MONITOR) && ENABLE_BATTERY_MONITOR
    // Ensure other battery configuration values have defaults if not explicitly set
    #ifndef BATTERY_VOLTAGE_DIVIDER
        #define BATTERY_VOLTAGE_DIVIDER     2.0   // Default 2:1 divider (override via build flag if needed)
    #endif
    #ifndef BATTERY_ADC_CALIBRATION
        #define BATTERY_ADC_CALIBRATION     1.0
    #endif
    #ifndef BATTERY_MIN_VOLTAGE
        #define BATTERY_MIN_VOLTAGE         3.0   // 1S LiPo minimum (empty)
    #endif
    #ifndef BATTERY_MAX_VOLTAGE
        #define BATTERY_MAX_VOLTAGE         4.2   // 1S LiPo maximum (full)
    #endif
    #ifndef BATTERY_SAMPLES
        #define BATTERY_SAMPLES             10
    #endif
#endif

// Data storage
#define MAX_LAPS_STORED     100   // Maximum laps to store in memory
#define MAX_PILOTS          2     // Maximum pilots in standalone mode

// RSSI history buffering for race data export
#define RSSI_HISTORY_ENABLED    1     // Enable RSSI history buffering (0 to disable)
// RSSI_HISTORY_SIZE is defined per-board above (S3 boards use 10K, others use 30K)
#ifndef RSSI_HISTORY_SIZE
    #define RSSI_HISTORY_SIZE   30000  // Default: 30,000 samples = 10 minutes @ 50Hz (150 KB)
#endif
#define RSSI_SAMPLE_INTERVAL_MS 20    // 50Hz = sample every 20ms

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
