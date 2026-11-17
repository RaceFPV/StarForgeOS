#include "CST820.h"
#include "platform/hal.h"

#include "driver/i2c.h"

namespace {
constexpr i2c_port_t kPort = I2C_NUM_0;
constexpr uint8_t kAddress = I2C_ADDR_CST820;

bool i2c_started = false;

void ensure_i2c(int sda, int scl) {
    if (i2c_started || sda < 0 || scl < 0) {
        return;
    }
    i2c_config_t cfg = {};
    cfg.mode = I2C_MODE_MASTER;
    cfg.sda_io_num = (gpio_num_t)sda;
    cfg.scl_io_num = (gpio_num_t)scl;
    cfg.sda_pullup_en = GPIO_PULLUP_ENABLE;
    cfg.scl_pullup_en = GPIO_PULLUP_ENABLE;
    cfg.master.clk_speed = 400000;
    i2c_param_config(kPort, &cfg);
    i2c_driver_install(kPort, I2C_MODE_MASTER, 0, 0, 0);
    i2c_started = true;
}

esp_err_t write_bytes(const uint8_t* data, size_t length) {
    return i2c_master_write_to_device(kPort, kAddress, data, length, pdMS_TO_TICKS(50));
}

esp_err_t read_bytes(uint8_t reg, uint8_t* data, size_t length) {
    return i2c_master_write_read_device(kPort, kAddress, &reg, 1, data, length, pdMS_TO_TICKS(50));
}
}

CST820::CST820(int8_t sda_pin, int8_t scl_pin, int8_t rst_pin, int8_t int_pin) {
    _sda = sda_pin;
    _scl = scl_pin;
    _rst = rst_pin;
    _int = int_pin;
}

void CST820::begin(void) {
    ensure_i2c(_sda, _scl);

    if (_int != -1) {
        hal_gpio_output(_int);
        hal_gpio_write(_int, 1);
        hal_delay_ms(1);
        hal_gpio_write(_int, 0);
        hal_delay_ms(1);
    }

    if (_rst != -1) {
        hal_gpio_output(_rst);
        hal_gpio_write(_rst, 0);
        hal_delay_ms(10);
        hal_gpio_write(_rst, 1);
        hal_delay_ms(300);
    }

    i2c_write(0xFE, 0xFF);
}

bool CST820::getTouch(uint16_t* x, uint16_t* y, uint8_t* gesture) {
    bool finger = (bool)i2c_read(0x02);
    *gesture = i2c_read(0x01);
    if (!(*gesture == SlideUp || *gesture == SlideDown)) {
        *gesture = None;
    }

    uint8_t data[4] = {0};
    i2c_read_continuous(0x03, data, 4);
    *x = ((data[0] & 0x0f) << 8) | data[1];
    *y = ((data[2] & 0x0f) << 8) | data[3];
    return finger;
}

uint8_t CST820::i2c_read(uint8_t addr) {
    uint8_t value = 0;
    for (int attempt = 0; attempt < 5; attempt++) {
        if (read_bytes(addr, &value, 1) == ESP_OK) {
            return value;
        }
    }
    return 0;
}

uint8_t CST820::i2c_read_continuous(uint8_t addr, uint8_t* data, uint32_t length) {
    if (read_bytes(addr, data, length) != ESP_OK) {
        return 1;
    }
    return 0;
}

void CST820::i2c_write(uint8_t addr, uint8_t data) {
    uint8_t payload[2] = {addr, data};
    write_bytes(payload, sizeof(payload));
}

uint8_t CST820::i2c_write_continuous(uint8_t addr, const uint8_t* data, uint32_t length) {
    uint8_t payload[16];
    if (length + 1 > sizeof(payload)) {
        return 1;
    }
    payload[0] = addr;
    for (uint32_t i = 0; i < length; i++) {
        payload[i + 1] = data[i];
    }
    return write_bytes(payload, length + 1) == ESP_OK ? 0 : 1;
}
