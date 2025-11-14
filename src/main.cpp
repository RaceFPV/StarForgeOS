#include <Arduino.h>
#include "config.h"
#include "config_loader.h"
#include "timing_core.h"
#include "standalone_mode.h"
#include "node_mode.h"
#include <WiFi.h>
#include <SPIFFS.h>

// Compile-time board detection verification
#if defined(BOARD_ESP32_S3_TOUCH)
    #pragma message "BUILD: Waveshare ESP32-S3-Touch-LCD-2 detected"
#elif defined(BOARD_JC2432W328C)
    #pragma message "BUILD: JC2432W328C detected"
#elif defined(CONFIG_IDF_TARGET_ESP32C6)
    #pragma message "BUILD: ESP32-C6 detected"
#elif defined(ARDUINO_ESP32C3_DEV) || defined(CONFIG_IDF_TARGET_ESP32C3)
    #pragma message "BUILD: ESP32-C3 detected"
#else
    #pragma message "BUILD: Generic ESP32 detected"
#endif

// Global firmware version strings (for compatibility with RotorHazard server)
const char *firmwareVersionString = "FIRMWARE_VERSION: ESP32_1.0.0";
const char *firmwareBuildDateString = "FIRMWARE_BUILDDATE: " __DATE__;
const char *firmwareBuildTimeString = "FIRMWARE_BUILDTIME: " __TIME__;
const char *firmwareProcTypeString = "FIRMWARE_PROCTYPE: ESP32";

// Global pin configuration (can be overridden by config.json on SPIFFS)
// These are initialized from config.h defaults, then potentially overridden at boot
uint8_t g_rssi_input_pin = RSSI_INPUT_PIN;
uint8_t g_rx5808_data_pin = RX5808_DATA_PIN;
uint8_t g_rx5808_clk_pin = RX5808_CLK_PIN;
uint8_t g_rx5808_sel_pin = RX5808_SEL_PIN;
uint8_t g_mode_switch_pin = MODE_SWITCH_PIN;

#if ENABLE_POWER_BUTTON
uint8_t g_power_button_pin = POWER_BUTTON_PIN;
#endif

#if ENABLE_BATTERY_MONITOR
uint8_t g_battery_adc_pin = BATTERY_ADC_PIN;
#endif

#if ENABLE_AUDIO
uint8_t g_audio_dac_pin = AUDIO_DAC_PIN;
#endif

#if defined(USB_DETECT_PIN)
uint8_t g_usb_detect_pin = USB_DETECT_PIN;
#endif

#if ENABLE_LCD_UI
int8_t g_lcd_i2c_sda = LCD_I2C_SDA;
int8_t g_lcd_i2c_scl = LCD_I2C_SCL;
int8_t g_lcd_backlight = LCD_BACKLIGHT;
#endif

// Global objects
TimingCore timing;
StandaloneMode standalone;
NodeMode node;

// Current operation mode
enum OperationMode {
  MODE_STANDALONE,
  MODE_ROTORHAZARD
};

OperationMode current_mode;
bool mode_initialized = false;

#if ENABLE_POWER_BUTTON
// Power button state tracking
unsigned long power_button_press_start = 0;
bool power_button_pressed = false;
bool deep_sleep_initiated = false;
#endif

#if ENABLE_LCD_UI
// For touch boards, mode is controlled by UI instead of physical pin
OperationMode requested_mode = MODE_STANDALONE;  // Default to standalone for LCD boards
#endif

// Function declarations
void checkModeSwitch();
void initializeMode();
void serialEvent();

#if ENABLE_POWER_BUTTON
void checkPowerButton();
void enterDeepSleep();
#endif

#if ENABLE_LCD_UI
// Function to request mode change from UI (touch boards only)
void requestModeChange(OperationMode new_mode);
#endif

void setup() {
  Serial.begin(UART_BAUD_RATE);
  delay(500);  // Longer delay to ensure all ESP-IDF boot messages complete
  
  // Clear any bootloader/ESP-IDF messages (ESP32 ROM bootloader + ESP-IDF errors)
  // This prevents garbage data from interfering with RotorHazard node detection
  while (Serial.available()) {
    Serial.read();
  }
  delay(200);  // Allow any remaining boot output to arrive
  while (Serial.available()) {
    Serial.read();
  }
  
  // Additional delay for RotorHazard mode to ensure boot messages are done
  // This prevents boot messages from being misinterpreted as protocol responses
  delay(300);
  
  // Load custom pin configuration from SPIFFS (if available)
  // This must happen BEFORE any pins are used or modes are initialized
  CustomPinConfig customConfig;
  if (ConfigLoader::loadCustomConfig(&customConfig)) {
    // Override pins with custom values from config.json
    g_rssi_input_pin = customConfig.rssi_input_pin;
    g_rx5808_data_pin = customConfig.rx5808_data_pin;
    g_rx5808_clk_pin = customConfig.rx5808_clk_pin;
    g_rx5808_sel_pin = customConfig.rx5808_sel_pin;
    g_mode_switch_pin = customConfig.mode_switch_pin;
    
    #if ENABLE_POWER_BUTTON
    if (customConfig.power_button_pin > 0) {
      g_power_button_pin = customConfig.power_button_pin;
    }
    #endif
    
    #if ENABLE_BATTERY_MONITOR
    if (customConfig.battery_adc_pin > 0) {
      g_battery_adc_pin = customConfig.battery_adc_pin;
    }
    #endif
    
    #if ENABLE_AUDIO
    if (customConfig.audio_dac_pin > 0) {
      g_audio_dac_pin = customConfig.audio_dac_pin;
    }
    #endif
    
    #if defined(USB_DETECT_PIN)
    if (customConfig.usb_detect_pin > 0) {
      g_usb_detect_pin = customConfig.usb_detect_pin;
    }
    #endif
    
    #if ENABLE_LCD_UI
    if (customConfig.lcd_i2c_sda >= 0) {
      g_lcd_i2c_sda = customConfig.lcd_i2c_sda;
    }
    if (customConfig.lcd_i2c_scl >= 0) {
      g_lcd_i2c_scl = customConfig.lcd_i2c_scl;
    }
    if (customConfig.lcd_backlight >= 0) {
      g_lcd_backlight = customConfig.lcd_backlight;
    }
    #endif
    
    Serial.println("Using custom pin configuration from /config.json");
  } else {
    Serial.println("Using default pin configuration from config.h");
  }
  
  // Debug: Print board configuration
  Serial.println("\n=== BOARD CONFIGURATION ===");
  #if defined(BOARD_ESP32_S3_TOUCH)
    Serial.println("Board: Waveshare ESP32-S3-Touch-LCD-2");
    Serial.printf("LCD Backlight Pin: %d\n", LCD_BACKLIGHT);
    Serial.printf("LCD I2C SDA: %d, SCL: %d\n", LCD_I2C_SDA, LCD_I2C_SCL);
  #elif defined(BOARD_JC2432W328C)
    Serial.println("Board: JC2432W328C");
  #else
    Serial.println("Board: Generic ESP32");
  #endif
  Serial.println("===========================\n");
  
#if ENABLE_LCD_UI
  // Touch board: Mode is controlled via touchscreen button, not physical pin
  // ALWAYS start in standalone mode on boot (LCD/WiFi mode)
  // This ensures users can always access the UI to switch modes, even after
  // a reset while in RotorHazard mode (where LCD is not visible)
  current_mode = MODE_STANDALONE;
  requested_mode = MODE_STANDALONE;  // Ensure both are in sync
  Serial.println("Touch board detected: Mode switch via LCD UI");
  Serial.println("Defaulting to STANDALONE mode (user can switch via LCD button)");
#else
  // Non-touch boards: Initialize mode selection pin (LOW=Standalone, HIGH/floating=RotorHazard)
  pinMode(g_mode_switch_pin, INPUT_PULLUP);
  
  // Determine initial mode BEFORE any serial output
  // LOW (GND) = Standalone mode, HIGH (floating/pullup) = RotorHazard mode
  bool initial_switch_state = digitalRead(g_mode_switch_pin);
  current_mode = (initial_switch_state == LOW) ? MODE_STANDALONE : MODE_ROTORHAZARD;
#endif
  
  // Only show startup messages in standalone mode (safe for debug output)
  if (current_mode == MODE_STANDALONE) {
    Serial.println();
    Serial.println("=== StarForge ESP32 Timer ===");
    Serial.println("Version: 1.0.0");
    Serial.println();
    Serial.println("Mode: STANDALONE/WIFI");
    Serial.println("Initializing timing core...");
    WiFi.softAP("SFOS", ""); // WE NEED THIS HERE FOR SOME DUMB REASON, OTHERWISE THE WIFI DOESN'T START UP CORRECTLY
  }
  
  // Initialize core timing system (creates task but keeps it INACTIVE)
  // On single-core ESP32-C3, timing task must NOT run until after mode initialization
  // Otherwise it starves serial/WiFi setup (same issue as WiFi AP initialization)
  timing.begin();
  
#if ENABLE_POWER_BUTTON
  // Initialize power button (pin 22 on JC2432W328C)
  pinMode(g_power_button_pin, INPUT_PULLUP);
  
  // Configure deep sleep wake-up on power button press (active LOW)
  esp_sleep_enable_ext0_wakeup((gpio_num_t)g_power_button_pin, 0);  // Wake when pin goes LOW
  
  Serial.printf("Power button enabled on GPIO%d (long press = sleep)\n", g_power_button_pin);
#endif
  
  // Set debug mode based on current mode
  bool debug_mode = (current_mode == MODE_STANDALONE);
  timing.setDebugMode(debug_mode);
  
  // Initialize mode-specific functionality BEFORE activating timing task
  initializeMode();
  
  // NOW activate timing core after mode setup is complete
  // This ensures serial port and WiFi are fully initialized before timing task runs
  timing.setActivated(true);
}

void loop() {
#if ENABLE_POWER_BUTTON
  // Check power button first (highest priority)
  checkPowerButton();
#endif
  
  // Check for mode changes
  checkModeSwitch();
  
  // Always process core timing (now handled by FreeRTOS task)
  timing.process();
  
  // Process mode-specific functions
  if (current_mode == MODE_STANDALONE) {
    standalone.process();
  } else {
    // In node mode, prioritize serial processing
    node.handleSerialInput();  // Direct call for faster response
    node.process();
  }
  
  // Handle serial communication again (belt and suspenders)
  serialEvent();
  
  // Very brief yield to allow timing task to run (ESP32-C3 single core)
  // Must be short to ensure responsive serial communication
  taskYIELD();  // Just yield, don't sleep - faster response
}

// Serial event handler (like Arduino)
void serialEvent() {
  // Only handle serial in RotorHazard node mode
  if (current_mode == MODE_ROTORHAZARD) {
    node.handleSerialInput();
  }
}

void checkModeSwitch() {
#if ENABLE_LCD_UI
  // Touch board: Check for mode change requests from UI button
  if (requested_mode != current_mode) {
    current_mode = requested_mode;
    // Update debug mode for timing core
    timing.setDebugMode(current_mode == MODE_STANDALONE);
    initializeMode();
  }
#else
  // Non-touch boards: Check physical mode switch pin
  static unsigned long last_check = 0;
  static bool last_switch_state = -1; // Initialize to invalid state to force first check
  
  // Only check every 100ms to avoid bouncing
  if (millis() - last_check < 100) {
    return;
  }
  
  bool current_switch_state = digitalRead(g_mode_switch_pin);
  
  if (current_switch_state != last_switch_state) {
    // Determine new mode
    // LOW (GND) = Standalone/WiFi mode, HIGH (floating/pullup) = RotorHazard mode (default)
    OperationMode new_mode;
    if (current_switch_state == LOW) {
      new_mode = MODE_STANDALONE;  // Switch to GND = Standalone mode (LCD active)
    } else {
      new_mode = MODE_ROTORHAZARD; // Switch floating/HIGH = RotorHazard mode
    }
    
    if (new_mode != current_mode || !mode_initialized) {
      current_mode = new_mode;
      // Update debug mode for timing core
      timing.setDebugMode(current_mode == MODE_STANDALONE);
      initializeMode();
    }
    
    last_switch_state = current_switch_state;
  }
  
  last_check = millis();
#endif
}

void initializeMode() {
  if (current_mode == MODE_STANDALONE) {
    // Only show debug output in standalone mode
    Serial.println("TimingCore: Ready");
    Serial.println();
    Serial.println("=== WIFI MODE ACTIVE ===");
    
    // Initialize standalone mode
    standalone.begin(&timing);
    
    // The standalone.begin() should output WiFi connection details
    Serial.println("Setup complete!");
    Serial.println();
    
  } else {
    // NODE MODE: NO debug output - it interferes with binary serial protocol
    // Silent initialization for RotorHazard compatibility
    
    // Initialize node mode (silently)
    node.begin(&timing);
    
    // Node mode is now active and waiting for RotorHazard commands
    // All communication is binary - no text output allowed
  }
  
  mode_initialized = true;
}

#if ENABLE_LCD_UI
// Function to request mode change from UI (touch boards only)
void requestModeChange(OperationMode new_mode) {
  requested_mode = new_mode;
  Serial.printf("UI: Mode change requested to %s\n", 
                new_mode == MODE_STANDALONE ? "STANDALONE" : "ROTORHAZARD");
}
#endif

#if ENABLE_POWER_BUTTON
// Check power button state and handle long press for deep sleep
void checkPowerButton() {
  bool button_state = digitalRead(g_power_button_pin);
  
  // Button pressed (active LOW)
  if (button_state == LOW) {
    if (!power_button_pressed) {
      // Button just pressed - start timing
      power_button_press_start = millis();
      power_button_pressed = true;
      deep_sleep_initiated = false;
    } else {
      // Button held down - check duration
      unsigned long press_duration = millis() - power_button_press_start;
      
      if (press_duration >= POWER_BUTTON_LONG_PRESS_MS && !deep_sleep_initiated) {
        // Long press detected - enter deep sleep
        deep_sleep_initiated = true;
        Serial.println("Power button long press detected - entering deep sleep...");
        Serial.println("Press power button to wake up");
        delay(100);  // Let serial output complete
        enterDeepSleep();
      }
    }
  } else {
    // Button released
    if (power_button_pressed) {
      unsigned long press_duration = millis() - power_button_press_start;
      
      // Short press - could be used for other functions if needed
      if (press_duration < POWER_BUTTON_LONG_PRESS_MS && press_duration > 50) {
        Serial.printf("Power button short press (%lu ms) - ignored\n", press_duration);
        // Optional: Could show a menu or toggle something here
      }
      
      power_button_pressed = false;
      deep_sleep_initiated = false;
    }
  }
}

// Enter deep sleep mode
void enterDeepSleep() {
#if ENABLE_LCD_UI
  // Turn off backlight to save power
  digitalWrite(g_lcd_backlight, LOW);
  
  // Show sleep message on LCD if possible
  // (Could add a simple "Sleeping..." message to lcd_ui if desired)
#endif
  
  // Flush serial
  Serial.flush();
  
  // Enter deep sleep - wake on power button press
  // The esp_sleep_enable_ext0_wakeup() was already configured in setup()
  esp_deep_sleep_start();
  
  // Code never reaches here - ESP32 will reset on wake
}
#endif

