#include <Arduino.h>
#include <lvgl.h>
#include <TFT_eSPI.h>
#include "CST820.h"

// Display
TFT_eSPI tft = TFT_eSPI();

// Touch - CST820 (capacitive, I2C)
// JC2432W328C CORRECT pins from factory example
#define I2C_SDA 33
#define I2C_SCL 32
#define TP_RST 25
#define TP_INT 21
CST820 touch(I2C_SDA, I2C_SCL, TP_RST, TP_INT);

// LVGL display buffer (optimized size for anti-aliasing)
static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[TFT_WIDTH * 60];  // 60 lines buffer (balanced for RAM)
static lv_disp_drv_t disp_drv;
static lv_indev_drv_t indev_drv;

// Forward declarations
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p);
void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data);
void createUI();

void setup() {
  Serial.begin(115200);
  delay(500);
  
  Serial.println("\n====================================");
  Serial.println("JC2432W328C LVGL + CST820 Test");
  Serial.println("====================================\n");
  
  // Initialize backlight (pin 27 on JC2432W328C!)
  pinMode(27, OUTPUT);
  digitalWrite(27, LOW);  // Turn off first
  Serial.println("Backlight: OFF (initializing)");
  
  // Initialize display
  Serial.println("Initializing TFT...");
  tft.begin();
  tft.setRotation(0);  // Portrait
  tft.initDMA();  // Enable DMA for non-blocking rendering
  Serial.println("TFT initialized (with DMA)");
  
  // Turn on backlight AFTER display init
  digitalWrite(27, HIGH);
  Serial.println("Backlight: ON");
  
  // Initialize LVGL
  Serial.println("Initializing LVGL...");
  lv_init();
  
  // Initialize LVGL display buffer
  lv_disp_draw_buf_init(&draw_buf, buf, NULL, TFT_WIDTH * 60);
  
  // Initialize display driver
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = TFT_WIDTH;
  disp_drv.ver_res = TFT_HEIGHT;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);
  
  Serial.println("LVGL display registered");
  
  // Initialize touch
  Serial.println("Initializing CST820 touch...");
  touch.begin();
  
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = my_touchpad_read;
  lv_indev_drv_register(&indev_drv);
  
  Serial.println("Touch initialized");
  
  // Create UI
  Serial.println("Creating UI...");
  createUI();
  
  Serial.println("\n====================================");
  Serial.println("Setup complete!");
  Serial.println("====================================\n");
  
  // Keep SPI transaction open for LVGL (factory example - enables DMA optimization)
  tft.startWrite();
}

void loop() {
  lv_timer_handler();  // LVGL task handler (must be called regularly)
  delay(5);
}

// LVGL display flush callback (DMA with wait to prevent artifacts)
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);

  // Use DMA but wait for completion to prevent tearing/artifacts
  tft.pushImageDMA(area->x1, area->y1, w, h, (uint16_t *)color_p);
  tft.dmaWait();  // Wait for DMA to complete before allowing next frame

  lv_disp_flush_ready(disp);
}

// LVGL touchpad read callback
void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
  uint16_t touchX, touchY;
  uint8_t gesture;
  
  bool touched = touch.getTouch(&touchX, &touchY, &gesture);
  
  if (touched) {
    data->state = LV_INDEV_STATE_PR;
    data->point.x = touchX;
    data->point.y = touchY;
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

// StarForge UI - Global UI elements for updates
lv_obj_t *rssi_label;
lv_obj_t *lap_count_label;

// Simple UI test - just essentials (240x320 portrait display)
void createUI() {
  // Create a screen with dark background
  lv_obj_t *scr = lv_obj_create(NULL);
  lv_scr_load(scr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
  lv_obj_set_style_pad_all(scr, 0, 0);  // Remove screen padding
  // Scrolling enabled again for user convenience
  
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
  lv_obj_set_style_bg_opa(rssi_title, LV_OPA_TRANSP, 0);  // Transparent background
  lv_obj_set_style_pad_all(rssi_title, 0, 0);
  lv_obj_set_pos(rssi_title, 10, 8);
  
  rssi_label = lv_label_create(rssi_box);
  lv_label_set_text(rssi_label, "45");
  lv_obj_set_style_text_font(rssi_label, &lv_font_montserrat_32, 0);
  lv_obj_set_style_text_color(rssi_label, lv_color_hex(0x00ff00), 0);
  lv_obj_set_style_bg_opa(rssi_label, LV_OPA_TRANSP, 0);  // Transparent background
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
  lv_obj_set_style_bg_opa(lap_title, LV_OPA_TRANSP, 0);  // Transparent background
  lv_obj_set_style_pad_all(lap_title, 0, 0);
  lv_obj_set_pos(lap_title, 10, 8);
  
  lap_count_label = lv_label_create(lap_box);
  lv_label_set_text(lap_count_label, "0");
  lv_obj_set_style_text_font(lap_count_label, &lv_font_montserrat_32, 0);
  lv_obj_set_style_text_color(lap_count_label, lv_color_hex(0xffffff), 0);
  lv_obj_set_style_bg_opa(lap_count_label, LV_OPA_TRANSP, 0);  // Transparent background
  lv_obj_set_style_pad_all(lap_count_label, 0, 0);
  lv_obj_set_pos(lap_count_label, 100, 30);
  
  // === BUTTONS (3 buttons stacked) ===
  // Start button
  lv_obj_t *start_btn = lv_btn_create(scr);
  lv_obj_set_size(start_btn, 220, 40);
  lv_obj_set_pos(start_btn, 10, 192);  // Add spacing: 110 + 70 + 12 = 192
  lv_obj_set_style_bg_color(start_btn, lv_color_hex(0x00aa00), 0);
  lv_obj_set_style_pad_all(start_btn, 0, 0);
  
  lv_obj_t *start_label = lv_label_create(start_btn);
  lv_label_set_text(start_label, "START");
  lv_obj_set_style_text_font(start_label, &lv_font_montserrat_16, 0);
  lv_obj_set_style_bg_opa(start_label, LV_OPA_TRANSP, 0);
  lv_obj_center(start_label);
  
  // Stop button
  lv_obj_t *stop_btn = lv_btn_create(scr);
  lv_obj_set_size(stop_btn, 220, 40);
  lv_obj_set_pos(stop_btn, 10, 239);  // 192 + 40 + 7 = 239
  lv_obj_set_style_bg_color(stop_btn, lv_color_hex(0xaa0000), 0);
  lv_obj_set_style_pad_all(stop_btn, 0, 0);
  
  lv_obj_t *stop_label = lv_label_create(stop_btn);
  lv_label_set_text(stop_label, "STOP");
  lv_obj_set_style_text_font(stop_label, &lv_font_montserrat_16, 0);
  lv_obj_set_style_bg_opa(stop_label, LV_OPA_TRANSP, 0);
  lv_obj_center(stop_label);
  
  // Clear button
  lv_obj_t *clear_btn = lv_btn_create(scr);
  lv_obj_set_size(clear_btn, 220, 40);
  lv_obj_set_pos(clear_btn, 10, 286);  // 239 + 40 + 7 = 286 (ends at 326, but close enough)
  lv_obj_set_style_bg_color(clear_btn, lv_color_hex(0x555555), 0);
  lv_obj_set_style_pad_all(clear_btn, 0, 0);
  
  lv_obj_t *clear_label = lv_label_create(clear_btn);
  lv_label_set_text(clear_label, "CLEAR");
  lv_obj_set_style_text_font(clear_label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_bg_opa(clear_label, LV_OPA_TRANSP, 0);
  lv_obj_center(clear_label);
  
  Serial.println("Simple UI: No padding, transparent labels");
}

