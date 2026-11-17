#include "platform/hal.h"
#include "config/config.h"
#include "config/config_loader.h"
#include "timing_core.h"
#include "standalone_mode.h"
#include <nvs_flash.h>
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Compile-time board detection verification
#if defined(BOARD_ESP32_S3_TOUCH)
    #pragma message "BUILD: Waveshare ESP32-S3-Touch-LCD-2 detected"
#elif defined(BOARD_JC2432W328C)
    #pragma message "BUILD: JC2432W328C detected"
#elif defined(BOARD_T_ENERGY)
    #pragma message "BUILD: LilyGo T-Energy (ESP32-S3) detected"
#elif defined(BOARD_NUCLEARCOUNTER)
    #pragma message "BUILD: NuclearCounter (ESP32-C3) detected"
#elif defined(BOARD_ESP32_C3_ZERO)
    #pragma message "BUILD: ESP32-C3 Zero detected"
#elif defined(CONFIG_IDF_TARGET_ESP32C6)
    #pragma message "BUILD: ESP32-C6 detected"
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
    #pragma message "BUILD: ESP32-C3 detected"
#else
    #pragma message "BUILD: Generic ESP32 detected"
#endif

// Global pin configuration (can be overridden by custom pin config in NVS)
// These are initialized from config.h defaults, then potentially overridden at boot
uint8_t g_rssi_input_pin = RSSI_INPUT_PIN;
uint8_t g_rx5808_data_pin = RX5808_DATA_PIN;
uint8_t g_rx5808_clk_pin = RX5808_CLK_PIN;
uint8_t g_rx5808_sel_pin = RX5808_SEL_PIN;

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

#if ENABLE_POWER_BUTTON
// Power button state tracking
unsigned long power_button_press_start = 0;
bool power_button_pressed = false;
bool deep_sleep_initiated = false;
#endif

// Function declarations
void initializeMode();

#if ENABLE_POWER_BUTTON
void checkPowerButton();
void enterDeepSleep();
#endif

void setup() {
  hal_delay_ms(200);
  log_fmt("StarForge %s\n", SF_FW_VERSION);
  
  // Initialize NVS (Non-Volatile Storage) early
  // This is needed for WiFi calibration and other system settings
  // If NVS was erased (e.g., by full chip erase), re-initialize it
  esp_err_t nvs_ret = nvs_flash_init();
  if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES || nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    // NVS partition was erased or needs to be re-initialized
    // This can happen if user did a full chip erase
    nvs_flash_erase();
    nvs_ret = nvs_flash_init();
  }
  // NVS errors are non-fatal - the device still runs with default calibration
  
  CustomPinConfig customConfig;
  if (ConfigLoader::loadCustomConfig(&customConfig, false)) {  // false = suppress Serial output
    // Override pins with custom values from NVS
    g_rssi_input_pin = customConfig.rssi_input_pin;
    g_rx5808_data_pin = customConfig.rx5808_data_pin;
    g_rx5808_clk_pin = customConfig.rx5808_clk_pin;
    g_rx5808_sel_pin = customConfig.rx5808_sel_pin;
    
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
  }
  
  if (ConfigLoader::hasCustomConfig()) {
    log_line("Using custom pin configuration from NVS");
  } else {
    log_line("Using default pin configuration from config.h");
  }
  
  log_line("\n=== BOARD CONFIGURATION ===");
    #if defined(BOARD_ESP32_S3_TOUCH)
      log_line("Board: Waveshare ESP32-S3-Touch-LCD-2");
      log_fmt("LCD Backlight Pin: %d\n", LCD_BACKLIGHT);
      log_fmt("LCD I2C SDA: %d, SCL: %d\n", LCD_I2C_SDA, LCD_I2C_SCL);
    #elif defined(BOARD_JC2432W328C)
      log_line("Board: JC2432W328C");
    #else
      log_line("Board: Generic ESP32");
    #endif
  log_line("===========================\n");
  
  hal_delay_ms(1000); // Give everything time to start up
  log_line("");
  log_line("=== StarForge ESP32 Timer ===");
  log_line("");
  log_line("Mode: WIFI");
  log_line("Initializing timing core...");
  
#if ENABLE_POWER_BUTTON
  // Initialize power button (pin 22 on JC2432W328C)
  hal_gpio_input_pullup(g_power_button_pin);
  
  // Configure deep sleep wake-up on power button press (active LOW)
  esp_sleep_enable_ext0_wakeup((gpio_num_t)g_power_button_pin, 0);  // Wake when pin goes LOW
  
  log_fmt("Power button enabled on GPIO%d (long press = sleep)\n", g_power_button_pin);
#endif
  
  timing.setDebugMode(true);
  
  // Start WiFi before the timing task so ESP32-WROOM-32D cache stays stable
  initializeMode();
  hal_delay_ms(500);
  timing.begin();
  timing.setActivated(true);
  log_line("Timing core activated and running");
}

void loop() {
#if ENABLE_POWER_BUTTON
  // Check power button first (highest priority)
  checkPowerButton();
#endif
  
  timing.process();
  standalone.process();
  taskYIELD();
}

void initializeMode() {
  log_line("=== WIFI MODE ACTIVE ===");
  standalone.begin(&timing);
  log_line("");
}

#if ENABLE_POWER_BUTTON
// Check power button state and handle long press for deep sleep
void checkPowerButton() {
  bool button_state = hal_gpio_read(g_power_button_pin);
  
  // Button pressed (active LOW)
  if (button_state == 0) {
    if (!power_button_pressed) {
      // Button just pressed - start timing
      power_button_press_start = hal_millis();
      power_button_pressed = true;
      deep_sleep_initiated = false;
    } else {
      // Button held down - check duration
      unsigned long press_duration = hal_millis() - power_button_press_start;
      
      if (press_duration >= POWER_BUTTON_LONG_PRESS_MS && !deep_sleep_initiated) {
        // Long press detected - enter deep sleep
        deep_sleep_initiated = true;
        log_line("Power button long press detected - entering deep sleep...");
        log_line("Press power button to wake up");
        hal_delay_ms(100);  // Let serial output complete
        enterDeepSleep();
      }
    }
  } else {
    // Button released
    if (power_button_pressed) {
      unsigned long press_duration = hal_millis() - power_button_press_start;
      
      // Short press - could be used for other functions if needed
      if (press_duration < POWER_BUTTON_LONG_PRESS_MS && press_duration > 50) {
        log_fmt("Power button short press (%lu ms) - ignored\n", press_duration);
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
  hal_gpio_write(g_lcd_backlight, 0);
  
  // Show sleep message on LCD if possible
  // (Could add a simple "Sleeping..." message to lcd_ui if desired)
#endif
  
  fflush(stdout);
  
  // Enter deep sleep - wake on power button press
  // The esp_sleep_enable_ext0_wakeup() was already configured in setup()
  esp_deep_sleep_start();
}
#endif

extern "C" void app_main(void) {
  setup();
  while (true) {
    loop();
  }
}

