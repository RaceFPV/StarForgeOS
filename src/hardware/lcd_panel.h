#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct LcdPanelPins {
    int mosi;
    int miso;
    int sclk;
    int cs;
    int dc;
    int rst;
    int clock_hz;
    bool invert;
};

bool lcd_panel_begin(const LcdPanelPins* pins);
void lcd_panel_draw(int x, int y, int width, int height, const void* pixels);
void lcd_panel_fill(uint16_t color);
