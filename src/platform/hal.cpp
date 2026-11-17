#include "platform/hal.h"

#include <cstdarg>
#include <cstdio>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/soc_caps.h"

#if SOC_DAC_SUPPORTED
#include "driver/dac.h"
#endif

uint32_t hal_millis() {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

uint32_t hal_micros() {
    return (uint32_t)esp_timer_get_time();
}

void hal_delay_ms(uint32_t ms) {
    if (ms == 0) {
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(ms));
}

void hal_delay_us(uint32_t us) {
    esp_rom_delay_us(us);
}

static bool pin_ok(int pin) {
    return pin >= 0 && pin < GPIO_NUM_MAX && GPIO_IS_VALID_GPIO(pin);
}

static void config_pin(int pin, gpio_mode_t mode, gpio_pullup_t pull) {
    if (!pin_ok(pin)) {
        return;
    }
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = 1ULL << pin;
    cfg.mode = mode;
    cfg.pull_up_en = pull;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&cfg);
}

void hal_gpio_output(int pin) {
    config_pin(pin, GPIO_MODE_OUTPUT, GPIO_PULLUP_DISABLE);
}

void hal_gpio_input(int pin) {
    config_pin(pin, GPIO_MODE_INPUT, GPIO_PULLUP_DISABLE);
}

void hal_gpio_input_pullup(int pin) {
    config_pin(pin, GPIO_MODE_INPUT, GPIO_PULLUP_ENABLE);
}

void hal_gpio_write(int pin, int level) {
    if (!pin_ok(pin)) {
        return;
    }
    gpio_set_level((gpio_num_t)pin, level ? 1 : 0);
}

int hal_gpio_read(int pin) {
    if (!pin_ok(pin)) {
        return 0;
    }
    return gpio_get_level((gpio_num_t)pin);
}

static adc_oneshot_unit_handle_t adc_units[2] = {nullptr, nullptr};

int hal_adc_read(int pin) {
    if (pin < 0) {
        return 0;
    }
    adc_unit_t unit_id;
    adc_channel_t channel;
    if (adc_oneshot_io_to_channel(pin, &unit_id, &channel) != ESP_OK) {
        return 0;
    }
    int index = (unit_id == ADC_UNIT_2) ? 1 : 0;
    if (!adc_units[index]) {
        adc_oneshot_unit_init_cfg_t unit_cfg = {};
        unit_cfg.unit_id = unit_id;
        if (adc_oneshot_new_unit(&unit_cfg, &adc_units[index]) != ESP_OK) {
            return 0;
        }
    }
    adc_oneshot_chan_cfg_t chan_cfg = {};
    chan_cfg.atten = ADC_ATTEN_DB_12;
    chan_cfg.bitwidth = ADC_BITWIDTH_12;
    adc_oneshot_config_channel(adc_units[index], channel, &chan_cfg);

    int raw = 0;
    if (adc_oneshot_read(adc_units[index], channel, &raw) != ESP_OK) {
        return 0;
    }
    return raw;
}

void hal_pwm_write(int pin, uint8_t duty) {
    if (!pin_ok(pin)) {
        return;
    }
    static bool timer_ready = false;
    if (!timer_ready) {
        ledc_timer_config_t timer = {};
        timer.speed_mode = LEDC_LOW_SPEED_MODE;
        timer.duty_resolution = LEDC_TIMER_8_BIT;
        timer.timer_num = LEDC_TIMER_0;
        timer.freq_hz = 5000;
        timer.clk_cfg = LEDC_AUTO_CLK;
        ledc_timer_config(&timer);
        timer_ready = true;
    }
    ledc_channel_config_t channel = {};
    channel.gpio_num = pin;
    channel.speed_mode = LEDC_LOW_SPEED_MODE;
    channel.channel = LEDC_CHANNEL_0;
    channel.timer_sel = LEDC_TIMER_0;
    channel.duty = duty;
    channel.hpoint = 0;
    ledc_channel_config(&channel);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

void hal_dac_write(int pin, uint8_t value) {
#if SOC_DAC_SUPPORTED
    dac_channel_t channel;
    if (pin == 25) {
        channel = DAC_CHANNEL_1;
    } else if (pin == 26) {
        channel = DAC_CHANNEL_2;
    } else {
        return;
    }
    dac_output_enable(channel);
    dac_output_voltage(channel, value);
#else
    (void)pin;
    (void)value;
#endif
}

long hal_map(long x, long in_min, long in_max, long out_min, long out_max) {
    if (in_max == in_min) {
        return out_min;
    }
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

void log_line(const char* message) {
    printf("%s\n", message ? message : "");
}

void log_raw(const char* message) {
    printf("%s", message ? message : "");
}

void log_fmt(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}
