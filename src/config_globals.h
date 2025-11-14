#ifndef CONFIG_GLOBALS_H
#define CONFIG_GLOBALS_H

#include <Arduino.h>
#include "config.h"

// Global pin configuration variables
// These are defined in main.cpp and initialized from config.h defaults
// They can be overridden at runtime by config.json on SPIFFS
extern uint8_t g_rssi_input_pin;
extern uint8_t g_rx5808_data_pin;
extern uint8_t g_rx5808_clk_pin;
extern uint8_t g_rx5808_sel_pin;
extern uint8_t g_mode_switch_pin;

#if ENABLE_POWER_BUTTON
extern uint8_t g_power_button_pin;
#endif

#if ENABLE_BATTERY_MONITOR
extern uint8_t g_battery_adc_pin;
#endif

#if ENABLE_AUDIO
extern uint8_t g_audio_dac_pin;
#endif

#if defined(USB_DETECT_PIN)
extern uint8_t g_usb_detect_pin;
#endif

#if ENABLE_LCD_UI
extern int8_t g_lcd_i2c_sda;
extern int8_t g_lcd_i2c_scl;
extern int8_t g_lcd_backlight;
#endif

#endif // CONFIG_GLOBALS_H

