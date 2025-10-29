#include "lcd_ui.h"

#if ENABLE_LCD_UI

#include "CST820.h"

// Static member initialization
LcdUI* LcdUI::_instance = nullptr;
lv_disp_draw_buf_t LcdUI::draw_buf;
lv_color_t LcdUI::buf[240 * 60];
lv_disp_drv_t LcdUI::disp_drv;
lv_indev_drv_t LcdUI::indev_drv;

LcdUI::LcdUI() : tft(nullptr), touch(nullptr), 
                 rssi_label(nullptr), lap_count_label(nullptr),
                 start_btn(nullptr), stop_btn(nullptr), clear_btn(nullptr),
                 _startCallback(nullptr), _stopCallback(nullptr), _clearCallback(nullptr) {
    _instance = this;
}

LcdUI::~LcdUI() {
    if (tft) delete tft;
    if (touch) delete touch;
    _instance = nullptr;
}

bool LcdUI::begin() {
    Serial.println("\n====================================");
    Serial.println("LCD UI: Initializing");
    Serial.println("====================================\n");
    
    // Initialize backlight (pin 27 on JC2432W328C)
    pinMode(LCD_BACKLIGHT, OUTPUT);
    digitalWrite(LCD_BACKLIGHT, LOW);  // Turn off first
    Serial.println("LCD: Backlight OFF (initializing)");
    
    // Initialize display
    Serial.println("LCD: Initializing TFT...");
    tft = new TFT_eSPI();
    tft->begin();
    tft->setRotation(0);  // Portrait
    // tft->initDMA();  // DMA disabled - causes issues with LVGL double buffering
    Serial.println("LCD: TFT initialized");
    
    // Turn on backlight AFTER display init
    digitalWrite(LCD_BACKLIGHT, HIGH);
    Serial.println("LCD: Backlight ON");
    
    // Initialize LVGL
    Serial.println("LCD: Initializing LVGL...");
    lv_init();
    
    // Initialize LVGL display buffer
    lv_disp_draw_buf_init(&draw_buf, buf, NULL, 240 * 60);
    
    // Initialize display driver
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = 240;
    disp_drv.ver_res = 320;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);
    
    Serial.println("LCD: LVGL display registered");
    
    // Initialize touch
    Serial.println("LCD: Initializing CST820 touch...");
    touch = new CST820(LCD_I2C_SDA, LCD_I2C_SCL, LCD_TOUCH_RST, LCD_TOUCH_INT);
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
    
    // Keep SPI transaction open for LVGL (factory example - enables DMA optimization)
    tft->startWrite();
    
    return true;
}

void LcdUI::createUI() {
    // Create a screen with dark background
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_scr_load(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    
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
    lv_obj_set_pos(rssi_label, 85, 35);
    
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
    
    Serial.println("LCD: UI created successfully");
}

// Update functions (called from standalone mode)
void LcdUI::updateRSSI(uint8_t rssi) {
    if (rssi_label) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", rssi);
        lv_label_set_text(rssi_label, buf);
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
    // Could update UI to show racing status (e.g., change border colors)
    // For now, just log it
    // Serial.printf("LCD: Race status: %s\n", racing ? "RACING" : "READY");
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

// LVGL display flush callback (DMA with wait to prevent artifacts)
void LcdUI::my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    if (!_instance || !_instance->tft) return;
    
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    // Use pushImage without DMA (simpler, no artifacts)
    _instance->tft->pushImage(area->x1, area->y1, w, h, (uint16_t *)color_p);

    lv_disp_flush_ready(disp);
}

// LVGL touchpad read callback
void LcdUI::my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
    if (!_instance || !_instance->touch) {
        data->state = LV_INDEV_STATE_REL;
        return;
    }
    
    uint16_t touchX, touchY;
    uint8_t gesture;
    
    bool touched = _instance->touch->getTouch(&touchX, &touchY, &gesture);
    
    if (touched) {
        data->state = LV_INDEV_STATE_PR;
        data->point.x = touchX;
        data->point.y = touchY;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

// UI task (runs in separate FreeRTOS task)
void LcdUI::uiTask(void* parameter) {
    Serial.println("LCD: UI task started");
    
    while (true) {
        lv_timer_handler();  // LVGL task handler (must be called regularly)
        vTaskDelay(pdMS_TO_TICKS(5));  // 5ms delay = ~200Hz update rate
    }
}

#endif // ENABLE_LCD_UI

