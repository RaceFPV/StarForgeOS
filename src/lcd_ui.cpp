#include "lcd_ui.h"

#if ENABLE_LCD_UI

#include "config_globals.h"
#include "CST820.h"
#include <FS.h>
#include <SPIFFS.h>

// Forward declaration of OperationMode enum from main.cpp
enum OperationMode {
  MODE_STANDALONE,
  MODE_ROTORHAZARD
};

// Static member initialization
LcdUI* LcdUI::_instance = nullptr;
lv_disp_draw_buf_t LcdUI::draw_buf;
lv_disp_drv_t LcdUI::disp_drv;
lv_indev_drv_t LcdUI::indev_drv;

#if defined(BOARD_ESP32_S3_TOUCH)
    // ESP32-S3: Dynamically allocated buffer (initialized in begin())
    lv_color_t* LcdUI::buf = nullptr;
#else
    // Other boards: Static buffer
    lv_color_t LcdUI::buf[240 * 60];
#endif

LcdUI::LcdUI() : 
#if defined(BOARD_ESP32_S3_TOUCH)
                 bus(nullptr), gfx(nullptr),
#else
                 tft(nullptr),
#endif
                 touch(nullptr), _timingCore(nullptr),
                 rssi_label(nullptr), rssi_chart(nullptr), rssi_series(nullptr),
                 lap_count_label(nullptr), status_label(nullptr),
                 battery_label(nullptr), battery_icon(nullptr),
                 start_btn(nullptr), stop_btn(nullptr), clear_btn(nullptr), mode_btn(nullptr),
                 band_label(nullptr), channel_label(nullptr), freq_label(nullptr), threshold_label(nullptr),
                 brightness_slider(nullptr), brightness_label(nullptr),
                 _startCallback(nullptr), _stopCallback(nullptr), _clearCallback(nullptr),
                 _settingsChangedCallback(nullptr),
                 _lastGraphUpdate(0), _lastScrollTime(0), _lastTouchTime(0), _screenDimmed(false), _userBrightness(100) {
    _instance = this;
}

LcdUI::~LcdUI() {
#if defined(BOARD_ESP32_S3_TOUCH)
    if (gfx) delete gfx;
    if (bus) delete bus;
#else
    if (tft) delete tft;
#endif
    if (touch) delete touch;
    _instance = nullptr;
}

bool LcdUI::begin() {
    Serial.println("\n====================================");
    Serial.println("LCD UI: Initializing");
    Serial.println("====================================\n");
    
    // Initialize backlight
    pinMode(g_lcd_backlight, OUTPUT);
    digitalWrite(g_lcd_backlight, LOW);  // Turn off first
    Serial.println("LCD: Backlight OFF (initializing)");
    
    // Initialize display (board-specific)
    Serial.println("LCD: Initializing display...");
    
#if defined(BOARD_ESP32_S3_TOUCH)
    // Waveshare ESP32-S3-Touch-LCD-2: Use Arduino_GFX with SPI2
    // Configuration matches official Waveshare demo
    Serial.println("LCD: Using Arduino_GFX for ESP32-S3");
    
    // Create SPI data bus (DC, CS, SCK, MOSI, MISO, SPI_NUM, isSPI_Mode)
    bus = new Arduino_ESP32SPI(42 /* DC */, 45 /* CS */, 39 /* SCK */, 38 /* MOSI */, 40 /* MISO */, FSPI /* spi_num */, true);
    
    // Create ST7789 display driver (bus, RST, rotation, IPS, width, height)
    // rotation=0 for portrait (matching UI layout), IPS=true for correct colors
    gfx = new Arduino_ST7789(bus, -1 /* RST */, 0 /* rotation */, true /* IPS */, 240 /* width */, 320 /* height */);
    
    // Initialize display (no invertDisplay, no special config - just like demo)
    if (!gfx->begin()) {
        Serial.println("ERROR: gfx->begin() failed!");
        return false;
    }
    
    gfx->fillScreen(BLACK);
    Serial.println("LCD: Arduino_GFX initialized");
    
#else
    // JC2432W328C and other boards: Use TFT_eSPI
    Serial.println("LCD: Using TFT_eSPI");
    
    tft = new TFT_eSPI();
    tft->begin();
    tft->setRotation(0);  // Portrait
    tft->fillScreen(TFT_BLACK);
    Serial.println("LCD: TFT_eSPI initialized");
    
#endif
    
    // Turn on backlight AFTER display init (use PWM for brightness control)
    pinMode(g_lcd_backlight, OUTPUT);
    
    // Load user brightness preference from SPIFFS
    loadBrightnessFromSPIFFS();
    
    // Convert percentage (10-100%) to PWM value (25-255)
    uint8_t pwm_value = map(_userBrightness, 10, 100, 25, 255);
    analogWrite(g_lcd_backlight, pwm_value);
    Serial.printf("LCD: Backlight ON (%d%% brightness)\n", _userBrightness);
    _lastTouchTime = millis();  // Initialize touch timer
    _screenDimmed = false;
    
    // Initialize LVGL
    Serial.println("LCD: Initializing LVGL...");
    lv_init();
    
#if defined(BOARD_ESP32_S3_TOUCH)
    // ESP32-S3: Allocate full screen buffer in PSRAM for smooth scrolling (240x320 = 76,800 pixels)
    uint32_t bufSize = 240 * 320;
    buf = (lv_color_t*)heap_caps_malloc(bufSize * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        // Fallback to internal RAM if PSRAM allocation fails
        Serial.println("LCD: PSRAM allocation failed, trying internal RAM...");
        buf = (lv_color_t*)heap_caps_malloc(bufSize * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!buf) {
        Serial.println("ERROR: Failed to allocate LVGL display buffer!");
        return false;
    }
    Serial.printf("LCD: Allocated %d KB buffer in %s\n", 
                  (bufSize * sizeof(lv_color_t)) / 1024,
                  heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) > (bufSize * sizeof(lv_color_t)) ? "PSRAM" : "Internal RAM");
    
    // Initialize LVGL display buffer (full screen for smooth performance)
    lv_disp_draw_buf_init(&draw_buf, buf, NULL, bufSize);
#else
    // Other boards: Use smaller buffer (60 lines)
    lv_disp_draw_buf_init(&draw_buf, buf, NULL, 240 * 60);
#endif
    
    // Initialize display driver
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = 240;
    disp_drv.ver_res = 320;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
#if defined(BOARD_ESP32_S3_TOUCH)
    // Enable direct mode for full-screen buffer (LVGL renders directly, we copy once)
    disp_drv.direct_mode = true;
#endif
    lv_disp_drv_register(&disp_drv);
    
    Serial.println("LCD: LVGL display registered");
    
    // Initialize touch
    Serial.println("LCD: Initializing CST820 touch...");
    touch = new CST820(g_lcd_i2c_sda, g_lcd_i2c_scl, LCD_TOUCH_RST, LCD_TOUCH_INT);
    touch->begin();
    
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    lv_indev_drv_register(&indev_drv);
    
    Serial.println("LCD: Touch initialized");
    
    // Create UI
    Serial.println("LCD: Creating UI...");
    createUI();
    
    Serial.println("\n====================================");
    Serial.println("LCD UI: Setup complete!");
    Serial.println("====================================\n");
    
#if !defined(BOARD_ESP32_S3_TOUCH)
    // Keep SPI transaction open for LVGL (TFT_eSPI optimization - not needed for Arduino_GFX)
    tft->startWrite();
#endif
    
    return true;
}

void LcdUI::createUI() {
    // Create a scrollable screen with dark background
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_scr_load(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_scroll_dir(scr, LV_DIR_VER);  // Enable vertical scrolling
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_size(scr, 240, 320);  // Physical screen size
    lv_obj_set_content_height(scr, 870);  // Increased for taller brightness panel
    
    // Disable elastic/bounce effect at top and bottom of scroll
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLL_ELASTIC);
    
    // === RSSI DISPLAY (Big box at top) ===
    lv_obj_t *rssi_box = lv_obj_create(scr);
    lv_obj_set_size(rssi_box, 220, 80);
    lv_obj_set_pos(rssi_box, 10, 20);
    lv_obj_set_style_bg_color(rssi_box, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_color(rssi_box, lv_color_hex(0x00ff00), 0);
    lv_obj_set_style_border_width(rssi_box, 2, 0);
    lv_obj_set_style_pad_all(rssi_box, 0, 0);
    lv_obj_clear_flag(rssi_box, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *rssi_title = lv_label_create(rssi_box);
    lv_label_set_text(rssi_title, "RSSI");
    lv_obj_set_style_text_color(rssi_title, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(rssi_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_opa(rssi_title, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(rssi_title, 0, 0);
    lv_obj_set_pos(rssi_title, 10, 8);
    
    rssi_label = lv_label_create(rssi_box);
    lv_label_set_text(rssi_label, "0");
    lv_obj_set_style_text_font(rssi_label, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(rssi_label, lv_color_hex(0x00ff00), 0);
    lv_obj_set_style_bg_opa(rssi_label, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(rssi_label, 0, 0);
    lv_obj_set_pos(rssi_label, 10, 30);  // Left side
    
    // RSSI Graph (right side)
    rssi_chart = lv_chart_create(rssi_box);
    lv_obj_set_size(rssi_chart, 140, 50);
    lv_obj_set_pos(rssi_chart, 75, 15);
    lv_chart_set_type(rssi_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_range(rssi_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 255);
    lv_chart_set_point_count(rssi_chart, 30);  // 30 points of history
    lv_chart_set_div_line_count(rssi_chart, 0, 0);  // No grid lines
    lv_obj_set_style_size(rssi_chart, 0, LV_PART_INDICATOR);  // No points
    lv_obj_set_style_bg_color(rssi_chart, lv_color_hex(0x0a0a0a), 0);
    lv_obj_set_style_border_width(rssi_chart, 1, 0);
    lv_obj_set_style_border_color(rssi_chart, lv_color_hex(0x333333), 0);
    lv_obj_set_style_pad_all(rssi_chart, 2, 0);
    
    // Add RSSI data series
    rssi_series = lv_chart_add_series(rssi_chart, lv_color_hex(0x00ff00), LV_CHART_AXIS_PRIMARY_Y);
    lv_obj_set_style_line_width(rssi_chart, 2, LV_PART_ITEMS);
    
    // Initialize with zeros
    for (int i = 0; i < 30; i++) {
        rssi_series->y_points[i] = 0;
    }
    lv_chart_refresh(rssi_chart);
    
#if ENABLE_BATTERY_MONITOR
    // Compact battery indicator (top-right corner, single line with icon + percentage)
    // Battery icon (small rectangle)
    battery_icon = lv_obj_create(rssi_box);
    lv_obj_set_size(battery_icon, 20, 12);
    lv_obj_set_pos(battery_icon, 145, 1);
    lv_obj_set_style_bg_color(battery_icon, lv_color_hex(0x888888), 0);
    lv_obj_set_style_border_width(battery_icon, 1, 0);
    lv_obj_set_style_border_color(battery_icon, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_radius(battery_icon, 2, 0);
    lv_obj_set_style_pad_all(battery_icon, 1, 0);
    lv_obj_clear_flag(battery_icon, LV_OBJ_FLAG_SCROLLABLE);
    
    // Battery percentage text (right next to icon, with gap for spacing)
    battery_label = lv_label_create(rssi_box);
    lv_label_set_text(battery_label, "---");  // Unknown initially
    lv_obj_set_style_text_font(battery_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(battery_label, lv_color_hex(0x888888), 0);
    lv_obj_set_style_bg_opa(battery_label, LV_OPA_TRANSP, 0);
    lv_obj_set_pos(battery_label, 182, 1);  // 145 + 20 (icon width) + 7 (spacing gap)
#endif
    
    // === LAP COUNT (Below RSSI) ===
    lv_obj_t *lap_box = lv_obj_create(scr);
    lv_obj_set_size(lap_box, 220, 70);
    lv_obj_set_pos(lap_box, 10, 110);
    lv_obj_set_style_bg_color(lap_box, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_width(lap_box, 1, 0);
    lv_obj_set_style_border_color(lap_box, lv_color_hex(0x333333), 0);
    lv_obj_set_style_pad_all(lap_box, 0, 0);
    lv_obj_clear_flag(lap_box, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *lap_title = lv_label_create(lap_box);
    lv_label_set_text(lap_title, "Laps");
    lv_obj_set_style_text_color(lap_title, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(lap_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_opa(lap_title, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(lap_title, 0, 0);
    lv_obj_set_pos(lap_title, 10, 8);
    
    lap_count_label = lv_label_create(lap_box);
    lv_label_set_text(lap_count_label, "0");
    lv_obj_set_style_text_font(lap_count_label, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(lap_count_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_opa(lap_count_label, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(lap_count_label, 0, 0);
    lv_obj_set_pos(lap_count_label, 100, 30);
    
    // Status label (right side of lap box)
    status_label = lv_label_create(lap_box);
    lv_label_set_text(status_label, "READY");
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(status_label, lv_color_hex(0x00ff00), 0);
    lv_obj_set_style_bg_opa(status_label, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(status_label, 0, 0);
    lv_obj_set_pos(status_label, 150, 8);
    
    // === BUTTONS (3 buttons stacked) ===
    // Start button
    start_btn = lv_btn_create(scr);
    lv_obj_set_size(start_btn, 220, 40);
    lv_obj_set_pos(start_btn, 10, 192);
    lv_obj_set_style_bg_color(start_btn, lv_color_hex(0x00aa00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(start_btn, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);  // Make background opaque
    lv_obj_set_style_pad_all(start_btn, 0, 0);
    lv_obj_add_event_cb(start_btn, start_btn_event, LV_EVENT_CLICKED, this);
    
    lv_obj_t *start_label = lv_label_create(start_btn);
    lv_label_set_text(start_label, "START");
    lv_obj_set_style_text_font(start_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_bg_opa(start_label, LV_OPA_TRANSP, 0);
    lv_obj_center(start_label);
    
    // Stop button
    stop_btn = lv_btn_create(scr);
    lv_obj_set_size(stop_btn, 220, 40);
    lv_obj_set_pos(stop_btn, 10, 239);
    lv_obj_set_style_bg_color(stop_btn, lv_color_hex(0xaa0000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(stop_btn, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);  // Make background opaque
    lv_obj_set_style_pad_all(stop_btn, 0, 0);
    lv_obj_add_event_cb(stop_btn, stop_btn_event, LV_EVENT_CLICKED, this);
    
    lv_obj_t *stop_label = lv_label_create(stop_btn);
    lv_label_set_text(stop_label, "STOP");
    lv_obj_set_style_text_font(stop_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_bg_opa(stop_label, LV_OPA_TRANSP, 0);
    lv_obj_center(stop_label);
    
    // Clear button
    clear_btn = lv_btn_create(scr);
    lv_obj_set_size(clear_btn, 220, 40);
    lv_obj_set_pos(clear_btn, 10, 286);
    lv_obj_set_style_bg_color(clear_btn, lv_color_hex(0x555555), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(clear_btn, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);  // Make background opaque
    lv_obj_set_style_pad_all(clear_btn, 0, 0);
    lv_obj_add_event_cb(clear_btn, clear_btn_event, LV_EVENT_CLICKED, this);
    
    lv_obj_t *clear_label = lv_label_create(clear_btn);
    lv_label_set_text(clear_label, "CLEAR");
    lv_obj_set_style_text_font(clear_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_opa(clear_label, LV_OPA_TRANSP, 0);
    lv_obj_center(clear_label);
    
    // Mode switch button (RotorHazard/Standalone)
    mode_btn = lv_btn_create(scr);
    lv_obj_set_size(mode_btn, 220, 40);
    lv_obj_set_pos(mode_btn, 10, 333);  // Below clear button
    lv_obj_set_style_bg_color(mode_btn, lv_color_hex(0x0055aa), LV_PART_MAIN | LV_STATE_DEFAULT);  // Blue for mode switch
    lv_obj_set_style_bg_opa(mode_btn, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(mode_btn, 0, 0);
    lv_obj_add_event_cb(mode_btn, mode_btn_event, LV_EVENT_CLICKED, this);
    
    lv_obj_t *mode_label = lv_label_create(mode_btn);
    lv_label_set_text(mode_label, "SWITCH TO ROTORHAZARD");
    lv_obj_set_style_text_font(mode_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_bg_opa(mode_label, LV_OPA_TRANSP, 0);
    lv_obj_center(mode_label);
    
    // === SETTINGS SECTION (Scroll down to see) ===
    // Section header
    lv_obj_t *settings_header = lv_label_create(scr);
    lv_label_set_text(settings_header, "--- SETTINGS ---");
    lv_obj_set_style_text_color(settings_header, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(settings_header, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_opa(settings_header, LV_OPA_TRANSP, 0);
    lv_obj_set_pos(settings_header, 60, 392);  // Moved down for mode button
    
    // Band selector (A/B/E/F/R/L)
    lv_obj_t *band_box = lv_obj_create(scr);
    lv_obj_set_size(band_box, 220, 78);  // Increased height for better button spacing
    lv_obj_set_pos(band_box, 10, 427);  // Adjusted position
    lv_obj_set_style_bg_color(band_box, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_width(band_box, 1, 0);
    lv_obj_set_style_border_color(band_box, lv_color_hex(0x333333), 0);
    lv_obj_set_style_pad_all(band_box, 8, 0);  // Increased padding for better spacing
    lv_obj_clear_flag(band_box, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *band_title = lv_label_create(band_box);
    lv_label_set_text(band_title, "Band");
    lv_obj_set_style_text_color(band_title, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(band_title, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(band_title, 5, 5);
    
    lv_obj_t *band_prev = lv_btn_create(band_box);
    lv_obj_set_size(band_prev, 40, 35);
    lv_obj_set_pos(band_prev, 10, 28);
    lv_obj_set_style_bg_color(band_prev, lv_color_hex(0x444444), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(band_prev, band_prev_event, LV_EVENT_CLICKED, this);
    lv_obj_t *band_prev_lbl = lv_label_create(band_prev);
    lv_label_set_text(band_prev_lbl, "<");
    lv_obj_center(band_prev_lbl);
    
    band_label = lv_label_create(band_box);
    lv_label_set_text(band_label, "A");
    lv_obj_set_style_text_font(band_label, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(band_label, lv_color_hex(0x00aaff), 0);
    lv_obj_set_pos(band_label, 90, 25);
    
    lv_obj_t *band_next = lv_btn_create(band_box);
    lv_obj_set_size(band_next, 40, 35);
    lv_obj_set_pos(band_next, 160, 28);
    lv_obj_set_style_bg_color(band_next, lv_color_hex(0x444444), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(band_next, band_next_event, LV_EVENT_CLICKED, this);
    lv_obj_t *band_next_lbl = lv_label_create(band_next);
    lv_label_set_text(band_next_lbl, ">");
    lv_obj_center(band_next_lbl);
    
    // Channel selector (1-8)
    lv_obj_t *channel_box = lv_obj_create(scr);
    lv_obj_set_size(channel_box, 220, 78);  // Increased height for better button spacing
    lv_obj_set_pos(channel_box, 10, 515);  // Adjusted position (427 + 78 + 10 gap)
    lv_obj_set_style_bg_color(channel_box, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_width(channel_box, 1, 0);
    lv_obj_set_style_border_color(channel_box, lv_color_hex(0x333333), 0);
    lv_obj_set_style_pad_all(channel_box, 8, 0);  // Increased padding for better spacing
    lv_obj_clear_flag(channel_box, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *channel_title = lv_label_create(channel_box);
    lv_label_set_text(channel_title, "Channel");
    lv_obj_set_style_text_color(channel_title, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(channel_title, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(channel_title, 5, 5);
    
    lv_obj_t *channel_prev = lv_btn_create(channel_box);
    lv_obj_set_size(channel_prev, 40, 35);
    lv_obj_set_pos(channel_prev, 10, 28);
    lv_obj_set_style_bg_color(channel_prev, lv_color_hex(0x444444), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(channel_prev, channel_prev_event, LV_EVENT_CLICKED, this);
    lv_obj_t *channel_prev_lbl = lv_label_create(channel_prev);
    lv_label_set_text(channel_prev_lbl, "<");
    lv_obj_center(channel_prev_lbl);
    
    channel_label = lv_label_create(channel_box);
    lv_label_set_text(channel_label, "1");
    lv_obj_set_style_text_font(channel_label, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(channel_label, lv_color_hex(0x00aaff), 0);
    lv_obj_set_pos(channel_label, 90, 25);
    
    lv_obj_t *channel_next = lv_btn_create(channel_box);
    lv_obj_set_size(channel_next, 40, 35);
    lv_obj_set_pos(channel_next, 160, 28);
    lv_obj_set_style_bg_color(channel_next, lv_color_hex(0x444444), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(channel_next, channel_next_event, LV_EVENT_CLICKED, this);
    lv_obj_t *channel_next_lbl = lv_label_create(channel_next);
    lv_label_set_text(channel_next_lbl, ">");
    lv_obj_center(channel_next_lbl);
    
    // Frequency display (read-only, updates based on band/channel)
    lv_obj_t *freq_box = lv_obj_create(scr);
    lv_obj_set_size(freq_box, 220, 55);  // Increased height for better spacing
    lv_obj_set_pos(freq_box, 10, 603);  // Adjusted position (515 + 78 + 10 gap)
    lv_obj_set_style_bg_color(freq_box, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_width(freq_box, 1, 0);
    lv_obj_set_style_border_color(freq_box, lv_color_hex(0x333333), 0);
    lv_obj_set_style_pad_all(freq_box, 8, 0);  // Added padding for consistency
    lv_obj_clear_flag(freq_box, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *freq_title = lv_label_create(freq_box);
    lv_label_set_text(freq_title, "Frequency:");
    lv_obj_set_style_text_color(freq_title, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(freq_title, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(freq_title, 10, 5);
    
    freq_label = lv_label_create(freq_box);
    lv_label_set_text(freq_label, "5865 MHz");
    lv_obj_set_style_text_font(freq_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(freq_label, lv_color_hex(0xffaa00), 0);
    lv_obj_set_pos(freq_label, 100, 14);
    
    // Threshold adjustment
    lv_obj_t *threshold_box = lv_obj_create(scr);
    lv_obj_set_size(threshold_box, 220, 78);  // Increased height for better button spacing
    lv_obj_set_pos(threshold_box, 10, 668);  // Adjusted position (603 + 55 + 10 gap)
    lv_obj_set_style_bg_color(threshold_box, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_width(threshold_box, 1, 0);
    lv_obj_set_style_border_color(threshold_box, lv_color_hex(0x333333), 0);
    lv_obj_set_style_pad_all(threshold_box, 8, 0);  // Increased padding for better spacing
    lv_obj_clear_flag(threshold_box, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t *threshold_title = lv_label_create(threshold_box);
    lv_label_set_text(threshold_title, "Threshold");
    lv_obj_set_style_text_color(threshold_title, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(threshold_title, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(threshold_title, 5, 5);
    
    lv_obj_t *threshold_dec = lv_btn_create(threshold_box);
    lv_obj_set_size(threshold_dec, 40, 35);
    lv_obj_set_pos(threshold_dec, 10, 28);
    lv_obj_set_style_bg_color(threshold_dec, lv_color_hex(0x444444), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(threshold_dec, threshold_dec_event, LV_EVENT_CLICKED, this);
    lv_obj_t *threshold_dec_lbl = lv_label_create(threshold_dec);
    lv_label_set_text(threshold_dec_lbl, "-");
    lv_obj_center(threshold_dec_lbl);
    
    threshold_label = lv_label_create(threshold_box);
    lv_label_set_text(threshold_label, "96");
    lv_obj_set_style_text_font(threshold_label, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(threshold_label, lv_color_hex(0xff00ff), 0);
    lv_obj_set_pos(threshold_label, 80, 25);
    
    lv_obj_t *threshold_inc = lv_btn_create(threshold_box);
    lv_obj_set_size(threshold_inc, 40, 35);
    lv_obj_set_pos(threshold_inc, 160, 28);
    lv_obj_set_style_bg_color(threshold_inc, lv_color_hex(0x444444), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(threshold_inc, threshold_inc_event, LV_EVENT_CLICKED, this);
    lv_obj_t *threshold_inc_lbl = lv_label_create(threshold_inc);
    lv_label_set_text(threshold_inc_lbl, "+");
    lv_obj_center(threshold_inc_lbl);
    
    // Brightness slider (at bottom of settings)
    lv_obj_t* brightness_box = lv_obj_create(scr);
    lv_obj_set_size(brightness_box, 220, 90);  // Increased height for better spacing
    lv_obj_set_pos(brightness_box, 10, 756);  // Adjusted position (668 + 78 + 10 gap)
    lv_obj_set_style_bg_color(brightness_box, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_width(brightness_box, 0, 0);
    lv_obj_set_style_radius(brightness_box, 8, 0);
    lv_obj_set_style_pad_all(brightness_box, 8, 0);  // Added padding for consistency
    lv_obj_clear_flag(brightness_box, LV_OBJ_FLAG_SCROLLABLE);  // Disable scrollbars on container
    
    lv_obj_t* brightness_title = lv_label_create(brightness_box);
    lv_label_set_text(brightness_title, "BRIGHTNESS");
    lv_obj_set_style_text_color(brightness_title, lv_color_hex(0xaaaaaa), 0);
    lv_obj_set_pos(brightness_title, 10, 5);
    
    brightness_label = lv_label_create(brightness_box);
    char brightness_buf[8];
    snprintf(brightness_buf, sizeof(brightness_buf), "%d%%", _userBrightness);
    lv_label_set_text(brightness_label, brightness_buf);
    lv_obj_set_style_text_font(brightness_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(brightness_label, lv_color_hex(0xffaa00), 0);
    lv_obj_set_pos(brightness_label, 175, 48);
    
    brightness_slider = lv_slider_create(brightness_box);
    lv_obj_set_size(brightness_slider, 155, 10);
    lv_obj_set_pos(brightness_slider, 10, 50);
    lv_slider_set_range(brightness_slider, 10, 100);  // 10-100%
    lv_slider_set_value(brightness_slider, _userBrightness, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(brightness_slider, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_bg_color(brightness_slider, lv_color_hex(0xffaa00), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(brightness_slider, lv_color_hex(0xffcc00), LV_PART_KNOB);
    lv_obj_add_flag(brightness_slider, LV_OBJ_FLAG_SCROLL_ON_FOCUS);  // Disable horizontal scrolling
    lv_obj_clear_flag(brightness_slider, LV_OBJ_FLAG_SCROLLABLE);     // Not scrollable
    lv_obj_add_event_cb(brightness_slider, brightness_slider_event, LV_EVENT_VALUE_CHANGED, this);
    
    Serial.println("LCD: UI created successfully");
}

// Update functions (called from standalone mode)
void LcdUI::updateRSSI(uint8_t rssi) {
    // Throttle RSSI updates to prevent overwhelming LVGL during scrolling
    // Update at same rate as graph (every 150ms) to prevent race conditions
    unsigned long now = millis();
    if (now - _lastGraphUpdate < GRAPH_UPDATE_INTERVAL) {
        return;  // Too soon, skip update
    }
    _lastGraphUpdate = now;
    
    // Update the numeric label
    if (rssi_label) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", rssi);
        lv_label_set_text(rssi_label, buf);
    }
    
    // Update graph
    if (rssi_chart && rssi_series) {
        lv_chart_set_next_value(rssi_chart, rssi_series, rssi);
        lv_chart_refresh(rssi_chart);
    }
}

void LcdUI::updateLapCount(uint16_t laps) {
    if (lap_count_label) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", laps);
        lv_label_set_text(lap_count_label, buf);
    }
}

void LcdUI::updateRaceStatus(bool racing) {
    if (status_label) {
        if (racing) {
            lv_label_set_text(status_label, "RACING");
            lv_obj_set_style_text_color(status_label, lv_color_hex(0xff0000), 0);  // Red when racing
        } else {
            lv_label_set_text(status_label, "READY");
            lv_obj_set_style_text_color(status_label, lv_color_hex(0x00ff00), 0);  // Green when ready
        }
    }
}

// Callback setters
void LcdUI::setStartCallback(void (*callback)()) {
    _startCallback = callback;
}

void LcdUI::setStopCallback(void (*callback)()) {
    _stopCallback = callback;
}

void LcdUI::setClearCallback(void (*callback)()) {
    _clearCallback = callback;
}

void LcdUI::setSettingsChangedCallback(void (*callback)()) {
    _settingsChangedCallback = callback;
}

void LcdUI::setTimingCore(TimingCore* core) {
    _timingCore = core;
}

// Settings update functions
void LcdUI::updateBandChannel(uint8_t band, uint8_t channel) {
    if (band_label) {
        const char* bands[] = {"A", "B", "E", "F", "R", "L"};
        if (band < 6) {
            lv_label_set_text(band_label, bands[band]);
        }
    }
    if (channel_label) {
        char buf[4];
        snprintf(buf, sizeof(buf), "%d", channel + 1);  // Display as 1-8 instead of 0-7
        lv_label_set_text(channel_label, buf);
    }
}

void LcdUI::updateFrequency(uint16_t freq_mhz) {
    if (freq_label) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d MHz", freq_mhz);
        lv_label_set_text(freq_label, buf);
    }
}

void LcdUI::updateThreshold(uint8_t threshold) {
    if (threshold_label) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", threshold);
        lv_label_set_text(threshold_label, buf);
    }
}

void LcdUI::updateBattery(float voltage, uint8_t percentage, bool isCharging) {
    // Update percentage text with charging indicator
    if (battery_label) {
        char buf[12];
        if (isCharging) {
            snprintf(buf, sizeof(buf), "%d%%+", percentage);  // + symbol for charging
        } else {
            snprintf(buf, sizeof(buf), "%d%%", percentage);
        }
        lv_label_set_text(battery_label, buf);
        
        // Color code based on battery level (cyan when charging)
        uint32_t color;
        if (isCharging) {
            color = 0x00ffff;  // Cyan (charging)
        } else if (percentage > 60) {
            color = 0x00ff00;  // Green (good)
        } else if (percentage > 20) {
            color = 0xffaa00;  // Orange (warning)
        } else {
            color = 0xff0000;  // Red (critical)
        }
        lv_obj_set_style_text_color(battery_label, lv_color_hex(color), 0);
    }
    
    // Update battery icon fill level and color
    if (battery_icon) {
        // Resize icon width based on percentage (max 30px)
        uint16_t width = (percentage * 30) / 100;
        if (width < 3) width = 3;  // Minimum visible width
        lv_obj_set_width(battery_icon, width);
        
        // Color code the icon (cyan when charging)
        uint32_t color;
        if (isCharging) {
            color = 0x00ffff;  // Cyan (charging)
        } else if (percentage > 60) {
            color = 0x00ff00;  // Green
        } else if (percentage > 20) {
            color = 0xffaa00;  // Orange
        } else {
            color = 0xff0000;  // Red
        }
        lv_obj_set_style_bg_color(battery_icon, lv_color_hex(color), 0);
    }
}



// Button event handlers (LVGL callbacks)
void LcdUI::start_btn_event(lv_event_t* e) {
    LcdUI* ui = (LcdUI*)e->user_data;
    if (ui && ui->_startCallback) {
        Serial.println("LCD: START button pressed");
        ui->_startCallback();
    }
}

void LcdUI::stop_btn_event(lv_event_t* e) {
    LcdUI* ui = (LcdUI*)e->user_data;
    if (ui && ui->_stopCallback) {
        Serial.println("LCD: STOP button pressed");
        ui->_stopCallback();
    }
}

void LcdUI::clear_btn_event(lv_event_t* e) {
    LcdUI* ui = (LcdUI*)e->user_data;
    if (ui && ui->_clearCallback) {
        Serial.println("LCD: CLEAR button pressed");
        ui->_clearCallback();
    }
}

// Mode button event handler - switches between RotorHazard and Standalone modes
void LcdUI::mode_btn_event(lv_event_t* e) {
    // Forward declaration of external function from main.cpp
    extern void requestModeChange(OperationMode new_mode);
    extern OperationMode current_mode;
    
    // Get button object to update label
    lv_obj_t* btn = lv_event_get_target(e);
    lv_obj_t* label = lv_obj_get_child(btn, 0);  // Get button label
    
    // Toggle mode
    if (current_mode == MODE_STANDALONE) {
        Serial.println("LCD: Switching to ROTORHAZARD mode");
        requestModeChange(MODE_ROTORHAZARD);
        // Note: The ESP32 will restart/reinit for RotorHazard mode
        // The LCD will show the RotorHazard mode until reinitialized
        lv_label_set_text(label, "Switching...");
    } else {
        Serial.println("LCD: Switching to STANDALONE mode");
        requestModeChange(MODE_STANDALONE);
        lv_label_set_text(label, "SWITCH TO ROTORHAZARD");
    }
}

// Settings button event handlers
void LcdUI::band_prev_event(lv_event_t* e) {
    LcdUI* ui = (LcdUI*)e->user_data;
    if (ui && ui->_timingCore) {
        uint8_t band, channel;
        ui->_timingCore->getRX5808Settings(band, channel);
        if (band > 0) band--;
        else band = 5;  // Wrap to L
        ui->_timingCore->setRX5808Settings(band, channel);
        Serial.printf("LCD: Band changed to %d\n", band);
        
        // Notify standalone mode to save settings
        if (ui->_settingsChangedCallback) {
            ui->_settingsChangedCallback();
        }
    }
}

void LcdUI::band_next_event(lv_event_t* e) {
    LcdUI* ui = (LcdUI*)e->user_data;
    if (ui && ui->_timingCore) {
        uint8_t band, channel;
        ui->_timingCore->getRX5808Settings(band, channel);
        if (band < 5) band++;
        else band = 0;  // Wrap to A
        ui->_timingCore->setRX5808Settings(band, channel);
        Serial.printf("LCD: Band changed to %d\n", band);
        
        // Notify standalone mode to save settings
        if (ui->_settingsChangedCallback) {
            ui->_settingsChangedCallback();
        }
    }
}

void LcdUI::channel_prev_event(lv_event_t* e) {
    LcdUI* ui = (LcdUI*)e->user_data;
    if (ui && ui->_timingCore) {
        uint8_t band, channel;
        ui->_timingCore->getRX5808Settings(band, channel);
        if (channel > 0) channel--;
        else channel = 7;  // Wrap to 8
        ui->_timingCore->setRX5808Settings(band, channel);
        Serial.printf("LCD: Channel changed to %d\n", channel + 1);
        
        // Notify standalone mode to save settings
        if (ui->_settingsChangedCallback) {
            ui->_settingsChangedCallback();
        }
    }
}

void LcdUI::channel_next_event(lv_event_t* e) {
    LcdUI* ui = (LcdUI*)e->user_data;
    if (ui && ui->_timingCore) {
        uint8_t band, channel;
        ui->_timingCore->getRX5808Settings(band, channel);
        if (channel < 7) channel++;
        else channel = 0;  // Wrap to 1
        ui->_timingCore->setRX5808Settings(band, channel);
        Serial.printf("LCD: Channel changed to %d\n", channel + 1);
        
        // Notify standalone mode to save settings
        if (ui->_settingsChangedCallback) {
            ui->_settingsChangedCallback();
        }
    }
}

void LcdUI::threshold_dec_event(lv_event_t* e) {
    LcdUI* ui = (LcdUI*)e->user_data;
    if (ui && ui->_timingCore) {
        uint8_t threshold = ui->_timingCore->getThreshold();
        if (threshold > 10) threshold -= 5;  // Decrement by 5
        ui->_timingCore->setThreshold(threshold);
        Serial.printf("LCD: Threshold decreased to %d\n", threshold);
        
        // Notify standalone mode to save settings
        if (ui->_settingsChangedCallback) {
            ui->_settingsChangedCallback();
        }
    }
}

void LcdUI::threshold_inc_event(lv_event_t* e) {
    LcdUI* ui = (LcdUI*)e->user_data;
    if (ui && ui->_timingCore) {
        uint8_t threshold = ui->_timingCore->getThreshold();
        if (threshold < 245) threshold += 5;  // Increment by 5
        ui->_timingCore->setThreshold(threshold);
        Serial.printf("LCD: Threshold increased to %d\n", threshold);
        
        // Notify standalone mode to save settings
        if (ui->_settingsChangedCallback) {
            ui->_settingsChangedCallback();
        }
    }
}


// LVGL display flush callback (works with both TFT_eSPI and Arduino_GFX)
void LcdUI::my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    if (!_instance) return;
    
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

#if defined(BOARD_ESP32_S3_TOUCH)
    // Arduino_GFX version with BGR color order (ST7789 on Waveshare uses BGR)
    if (!_instance->gfx) return;
    _instance->gfx->draw16bitBeRGBBitmap(area->x1, area->y1, (uint16_t *)color_p, w, h);
#else
    // TFT_eSPI version
    if (!_instance->tft) return;
    _instance->tft->pushImage(area->x1, area->y1, w, h, (uint16_t *)color_p);
#endif

    lv_disp_flush_ready(disp);
}

// LVGL touchpad read callback
void LcdUI::my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
    if (!_instance || !_instance->touch) {
        data->state = LV_INDEV_STATE_REL;
        return;
    }
    
    // Throttle touch reads to prevent overwhelming LVGL during fast scrolling
    // This prevents use-after-free crashes when scrolling rapidly
    static unsigned long last_touch_read = 0;
    static lv_indev_data_t last_data = {0};  // Cache last state
    unsigned long now = millis();
    
    if (now - last_touch_read < 10) {  // Minimum 10ms between reads (~100Hz, more responsive)
        // Too soon, return cached state to avoid flooding LVGL
        *data = last_data;
        return;
    }
    last_touch_read = now;
    
    uint16_t touchX, touchY;
    uint8_t gesture;
    
    bool touched = _instance->touch->getTouch(&touchX, &touchY, &gesture);
    
    if (touched) {
        data->state = LV_INDEV_STATE_PR;
        data->point.x = touchX;
        data->point.y = touchY;
        
        // Track scroll time to pause RSSI updates during scrolling
        _instance->_lastScrollTime = now;
        
        // Wake screen on touch
        _instance->wakeScreen();
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
    
    // Cache state for throttled calls
    last_data = *data;
}

// UI task (runs in separate FreeRTOS task)
void LcdUI::uiTask(void* parameter) {
    Serial.println("LCD: UI task started");
    
    while (true) {
        lv_timer_handler();  // LVGL task handler (must be called regularly)
        
        // Update screen brightness (dim after timeout)
        if (_instance) {
            _instance->updateScreenBrightness();
        }
        
        // 5ms delay to prevent race conditions in LVGL (not thread-safe)
        // Longer delay = more stable during rapid scrolling, still 200Hz update rate
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// Update screen brightness based on activity timeout
void LcdUI::updateScreenBrightness() {
    unsigned long now = millis();
    unsigned long timeSinceTouch = now - _lastTouchTime;
    
    // Check if we should dim the screen
    if (!_screenDimmed && timeSinceTouch >= SCREEN_DIM_TIMEOUT) {
        // Time to dim
        analogWrite(g_lcd_backlight, SCREEN_DIM_BRIGHTNESS);
        _screenDimmed = true;
        Serial.println("LCD: Screen dimmed (power save)");
    }
}

// Wake screen to full brightness
void LcdUI::wakeScreen() {
    _lastTouchTime = millis();
    
    // Only update if screen was dimmed (avoid unnecessary PWM writes)
    if (_screenDimmed) {
        setBrightness(_userBrightness);  // Restore user's brightness
        _screenDimmed = false;
        Serial.println("LCD: Screen woke up (touch detected)");
    }
}

// Set brightness (10-100%)
void LcdUI::setBrightness(uint8_t percent) {
    // Clamp to valid range
    if (percent < 10) percent = 10;
    if (percent > 100) percent = 100;
    
    _userBrightness = percent;
    
    // Convert percentage (10-100%) to PWM value (25-255)
    uint8_t pwm_value = map(percent, 10, 100, 25, 255);
    analogWrite(g_lcd_backlight, pwm_value);
}

// Load brightness from SPIFFS
void LcdUI::loadBrightnessFromSPIFFS() {
    fs::File file = SPIFFS.open("/brightness.txt", "r");
    if (file) {
        String content = file.readStringUntil('\n');
        _userBrightness = content.toInt();
        
        // Validate range
        if (_userBrightness < 10 || _userBrightness > 100) {
            _userBrightness = 100;  // Default to 100%
        }
        
        file.close();
        Serial.printf("LCD: Loaded brightness from SPIFFS: %d%%\n", _userBrightness);
    } else {
        _userBrightness = 100;  // Default to 100%
        Serial.println("LCD: No saved brightness, using default 100%");
    }
}

// Save brightness to SPIFFS
void LcdUI::saveBrightnessToSPIFFS() {
    fs::File file = SPIFFS.open("/brightness.txt", "w");
    if (file) {
        file.println(_userBrightness);
        file.close();
        Serial.printf("LCD: Saved brightness to SPIFFS: %d%%\n", _userBrightness);
    } else {
        Serial.println("LCD: Failed to save brightness to SPIFFS");
    }
}

// Brightness slider event
void LcdUI::brightness_slider_event(lv_event_t* e) {
    if (!_instance) return;
    
    lv_obj_t* slider = lv_event_get_target(e);
    int32_t value = lv_slider_get_value(slider);
    
    // Update label
    if (_instance->brightness_label) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%ld%%", value);
        lv_label_set_text(_instance->brightness_label, buf);
    }
    
    // Apply brightness immediately
    _instance->setBrightness((uint8_t)value);
    
    // Save to SPIFFS
    _instance->saveBrightnessToSPIFFS();
    
    // Reset dim timer (keep screen on while adjusting)
    _instance->_lastTouchTime = millis();
    _instance->_screenDimmed = false;
}

#endif // ENABLE_LCD_UI


