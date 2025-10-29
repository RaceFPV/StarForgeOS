#ifndef LCD_UI_H
#define LCD_UI_H

#include "config.h"

#if ENABLE_LCD_UI

#include <Arduino.h>
#include <lvgl.h>
#include <TFT_eSPI.h>
#include "timing_core.h"

// Forward declaration of CST820 class
class CST820;

class LcdUI {
public:
    LcdUI();
    ~LcdUI();
    
    // Initialize LCD and LVGL
    bool begin();
    
    // Update UI with current state (called by standalone mode)
    void updateRSSI(uint8_t rssi);
    void updateLapCount(uint16_t laps);
    void updateRaceStatus(bool racing);
    
    // Set button callbacks (called by standalone mode when buttons pressed)
    void setStartCallback(void (*callback)());
    void setStopCallback(void (*callback)());
    void setClearCallback(void (*callback)());
    
    // Task handler (runs in separate FreeRTOS task)
    static void uiTask(void* parameter);
    
private:
    // Display and touch objects
    TFT_eSPI* tft;
    CST820* touch;
    
    // LVGL objects
    lv_obj_t* rssi_label;
    lv_obj_t* lap_count_label;
    lv_obj_t* start_btn;
    lv_obj_t* stop_btn;
    lv_obj_t* clear_btn;
    
    // Callbacks for button events
    void (*_startCallback)();
    void (*_stopCallback)();
    void (*_clearCallback)();
    
    // LVGL display buffer
    static lv_disp_draw_buf_t draw_buf;
    static lv_color_t buf[240 * 60];  // 60 lines buffer
    static lv_disp_drv_t disp_drv;
    static lv_indev_drv_t indev_drv;
    
    // Static callbacks for LVGL
    static void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p);
    static void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data);
    
    // Button event handlers (LVGL callbacks)
    static void start_btn_event(lv_event_t* e);
    static void stop_btn_event(lv_event_t* e);
    static void clear_btn_event(lv_event_t* e);
    
    // Create UI elements
    void createUI();
    
    // Singleton instance for static callbacks
    static LcdUI* _instance;
};

#endif // ENABLE_LCD_UI
#endif // LCD_UI_H

