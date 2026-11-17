#include "board_displays.h"

#if defined(BOARD_NUCLEARCOUNTER)
#include <cstdio>
#include <cstring>

#include "driver/i2c.h"
#include "platform/hal.h"

namespace {
constexpr uint8_t kAddress = 0x3C;

void oled_cmd(uint8_t cmd) {
    uint8_t payload[2] = {0x00, cmd};
    i2c_master_write_to_device(I2C_NUM_0, kAddress, payload, sizeof(payload), pdMS_TO_TICKS(50));
}

void oled_cmd2(uint8_t cmd, uint8_t arg) {
    uint8_t payload[3] = {0x00, cmd, arg};
    i2c_master_write_to_device(I2C_NUM_0, kAddress, payload, sizeof(payload), pdMS_TO_TICKS(50));
}

// 5x7 glyphs for the characters the status screen uses.
uint8_t glyph(char c, int column) {
    static const uint8_t font[][5] = {
        {0x00, 0x00, 0x00, 0x00, 0x00},
        {0x7E, 0x11, 0x11, 0x11, 0x7E}, {0x7F, 0x49, 0x49, 0x49, 0x36},
        {0x3E, 0x41, 0x41, 0x41, 0x22}, {0x7F, 0x41, 0x41, 0x22, 0x1C},
        {0x7F, 0x49, 0x49, 0x49, 0x41}, {0x7F, 0x09, 0x09, 0x09, 0x01},
        {0x3E, 0x41, 0x49, 0x49, 0x7A}, {0x7F, 0x08, 0x08, 0x08, 0x7F},
        {0x00, 0x41, 0x7F, 0x41, 0x00}, {0x20, 0x40, 0x41, 0x3F, 0x01},
        {0x7F, 0x08, 0x14, 0x22, 0x41}, {0x7F, 0x40, 0x40, 0x40, 0x40},
        {0x7F, 0x02, 0x0C, 0x02, 0x7F}, {0x7F, 0x04, 0x08, 0x10, 0x7F},
        {0x3E, 0x41, 0x41, 0x41, 0x3E}, {0x7F, 0x09, 0x09, 0x09, 0x06},
        {0x3E, 0x41, 0x51, 0x21, 0x5E}, {0x7F, 0x09, 0x19, 0x29, 0x46},
        {0x46, 0x49, 0x49, 0x49, 0x31}, {0x01, 0x01, 0x7F, 0x01, 0x01},
        {0x3F, 0x40, 0x40, 0x40, 0x3F}, {0x1F, 0x20, 0x40, 0x20, 0x1F},
        {0x3F, 0x40, 0x38, 0x40, 0x3F}, {0x63, 0x14, 0x08, 0x14, 0x63},
        {0x07, 0x08, 0x70, 0x08, 0x07}, {0x61, 0x51, 0x49, 0x45, 0x43},
    };
    if (c >= 'a' && c <= 'z') {
        c = (char)(c - 32);
    }
    if (c == ' ') {
        return 0;
    }
    if (c == ':') {
        return column == 1 || column == 2 ? 0x24 : 0;
    }
    if (c == '.') {
        return column == 2 ? 0x40 : 0;
    }
    if (c == '-') {
        return column == 0 || column == 4 ? 0 : 0x08;
    }
    if (c >= '0' && c <= '9') {
        static const uint8_t digits[][5] = {
            {0x3E, 0x51, 0x49, 0x45, 0x3E}, {0x00, 0x42, 0x7F, 0x40, 0x00},
            {0x42, 0x61, 0x51, 0x49, 0x46}, {0x21, 0x41, 0x45, 0x4B, 0x31},
            {0x18, 0x14, 0x12, 0x7F, 0x10}, {0x27, 0x45, 0x45, 0x45, 0x39},
            {0x3C, 0x4A, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03},
            {0x36, 0x49, 0x49, 0x49, 0x36}, {0x06, 0x49, 0x49, 0x29, 0x1E},
        };
        return digits[c - '0'][column];
    }
    if (c >= 'A' && c <= 'Z') {
        return font[1 + (c - 'A')][column];
    }
    return 0;
}

void draw_text(uint8_t* frame, int x, int page, const char* text) {
    for (int i = 0; text[i] && x < 122; i++) {
        for (int col = 0; col < 5 && x < 128; col++, x++) {
            frame[page * 128 + x] = glyph(text[i], col);
        }
        x++;
    }
}
}

void BoardDisplays::initNuclearCounter(const char* ssid) {
    i2c_config_t cfg = {};
    cfg.mode = I2C_MODE_MASTER;
    cfg.sda_io_num = GPIO_NUM_8;
    cfg.scl_io_num = GPIO_NUM_9;
    cfg.sda_pullup_en = GPIO_PULLUP_ENABLE;
    cfg.scl_pullup_en = GPIO_PULLUP_ENABLE;
    cfg.master.clk_speed = 400000;
    i2c_param_config(I2C_NUM_0, &cfg);
    i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0);

    oled_cmd(0xAE);
    oled_cmd2(0xD5, 0x80);
    oled_cmd2(0xA8, 0x3F);
    oled_cmd2(0xD3, 0x00);
    oled_cmd(0x40);
    oled_cmd2(0xAD, 0x8B);
    oled_cmd(0xA1);
    oled_cmd(0xC8);
    oled_cmd2(0xDA, 0x12);
    oled_cmd2(0x81, 0x80);
    oled_cmd2(0xD9, 0xF1);
    oled_cmd2(0xDB, 0x40);
    oled_cmd(0xA4);
    oled_cmd(0xA6);

    uint8_t frame[128 * 8];
    memset(frame, 0, sizeof(frame));
    draw_text(frame, 0, 0, "NuclearCounter SFOS");
    draw_text(frame, 0, 1, "IP: 192.168.4.1");
    char ssid_line[24];
    snprintf(ssid_line, sizeof(ssid_line), "SSID: %s", ssid ? ssid : "");
    draw_text(frame, 0, 2, ssid_line);
    draw_text(frame, 0, 3, "Status: READY");

    for (uint8_t page = 0; page < 8; page++) {
        oled_cmd(0xB0 | page);
        oled_cmd(0x02);
        oled_cmd(0x10);
        uint8_t payload[129];
        payload[0] = 0x40;
        memcpy(&payload[1], &frame[page * 128], 128);
        i2c_master_write_to_device(I2C_NUM_0, kAddress, payload, sizeof(payload), pdMS_TO_TICKS(100));
    }
    oled_cmd(0xAF);
}
#endif

BoardDisplays::BoardDisplays() {}
