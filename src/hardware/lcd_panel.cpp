#include "lcd_panel.h"

#include <string.h>

#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"

static esp_lcd_panel_handle_t panel = NULL;

bool lcd_panel_begin(const LcdPanelPins* pins) {
    if (!pins) {
        return false;
    }

    spi_bus_config_t bus = {};
    bus.mosi_io_num = pins->mosi;
    bus.miso_io_num = pins->miso;
    bus.sclk_io_num = pins->sclk;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = 240 * 80 * (int)sizeof(uint16_t);
    if (spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) {
        return false;
    }

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_spi_config_t io_cfg = {};
    io_cfg.cs_gpio_num = pins->cs;
    io_cfg.dc_gpio_num = pins->dc;
    io_cfg.spi_mode = 0;
    io_cfg.pclk_hz = pins->clock_hz;
    io_cfg.trans_queue_depth = 10;
    io_cfg.lcd_cmd_bits = 8;
    io_cfg.lcd_param_bits = 8;
    if (esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_cfg, &io) != ESP_OK) {
        return false;
    }

    esp_lcd_panel_dev_config_t panel_cfg = {};
    panel_cfg.reset_gpio_num = pins->rst;
    panel_cfg.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_cfg.bits_per_pixel = 16;
    if (esp_lcd_new_panel_st7789(io, &panel_cfg, &panel) != ESP_OK) {
        return false;
    }

    esp_lcd_panel_reset(panel);
    esp_lcd_panel_init(panel);
    esp_lcd_panel_invert_color(panel, pins->invert);
    esp_lcd_panel_disp_on_off(panel, true);
    lcd_panel_fill(0);
    return true;
}

void lcd_panel_draw(int x, int y, int width, int height, const void* pixels) {
    if (!panel || !pixels || width <= 0 || height <= 0) {
        return;
    }
    esp_lcd_panel_draw_bitmap(panel, x, y, x + width, y + height, pixels);
}

void lcd_panel_fill(uint16_t color) {
    if (!panel) {
        return;
    }
    uint16_t line[240];
    for (int i = 0; i < 240; i++) {
        line[i] = color;
    }
    for (int y = 0; y < 320; y += 8) {
        uint16_t block[240 * 8];
        for (int row = 0; row < 8; row++) {
            memcpy(&block[row * 240], line, sizeof(line));
        }
        int height = (y + 8 <= 320) ? 8 : (320 - y);
        esp_lcd_panel_draw_bitmap(panel, 0, y, 240, y + height, block);
    }
}
