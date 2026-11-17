#pragma once

#include <cstdint>
#include <cstddef>

// Timing and GPIO on top of ESP-IDF. This is not the Arduino core.

uint32_t hal_millis();
uint32_t hal_micros();
void hal_delay_ms(uint32_t ms);
void hal_delay_us(uint32_t us);

void hal_gpio_output(int pin);
void hal_gpio_input(int pin);
void hal_gpio_input_pullup(int pin);
void hal_gpio_write(int pin, int level);
int hal_gpio_read(int pin);

// 12-bit oneshot read, 0-3.3V attenuation. Returns 0 when the pin is not an ADC pin.
int hal_adc_read(int pin);

// 8-bit LEDC duty on a shared 5 kHz timer.
void hal_pwm_write(int pin, uint8_t duty);

// 8-bit DAC write. Only the original ESP32 has a DAC; other targets ignore the call.
void hal_dac_write(int pin, uint8_t value);

long hal_map(long x, long in_min, long in_max, long out_min, long out_max);

void log_line(const char* message = "");
void log_raw(const char* message);
void log_fmt(const char* fmt, ...);
