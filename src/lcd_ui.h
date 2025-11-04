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
    void updateBandChannel(uint8_t band, uint8_t channel);
    void updateFrequency(uint16_t freq_mhz);
    void updateThreshold(uint8_t threshold);
    void updateBattery(float voltage, uint8_t percentage);  // For custom PCB with voltage divider
    
    // Link timing core for settings access
    void setTimingCore(TimingCore* core);
    
    // Set button callbacks (called by standalone mode when buttons pressed)
    void setStartCallback(void (*callback)());
    void setStopCallback(void (*callback)());
    void setClearCallback(void (*callback)());
    void setSettingsChangedCallback(void (*callback)());  // Called when user changes settings
    
    // Task handler (runs in separate FreeRTOS task)
    static void uiTask(void* parameter);
    
private:
    // Display and touch objects
    TFT_eSPI* tft;
    CST820* touch;
    TimingCore* _timingCore;  // For accessing RX5808 settings
    
    // LVGL objects
    lv_obj_t* rssi_label;
    lv_obj_t* rssi_chart;  // RSSI history graph
    lv_chart_series_t* rssi_series;  // RSSI data series
    lv_obj_t* lap_count_label;
    lv_obj_t* status_label;  // Shows "READY", "RACING", "STOPPED"
    lv_obj_t* battery_label;  // Shows battery percentage
    lv_obj_t* battery_icon;   // Battery icon visualization
    lv_obj_t* start_btn;
    lv_obj_t* stop_btn;
    lv_obj_t* clear_btn;
    lv_obj_t* mode_btn;  // Mode switch button (RotorHazard/Standalone)
    
    // Settings UI elements (scrollable section)
    lv_obj_t* band_label;
    lv_obj_t* channel_label;
    lv_obj_t* freq_label;
    lv_obj_t* threshold_label;
    lv_obj_t* brightness_slider;
    lv_obj_t* brightness_label;
    
    // Callbacks for button events
    void (*_startCallback)();
    void (*_stopCallback)();
    void (*_clearCallback)();
    void (*_settingsChangedCallback)();  // Called when user changes settings
    
    // RSSI graph rate limiting
    unsigned long _lastGraphUpdate;
    static const unsigned long GRAPH_UPDATE_INTERVAL = 150;  // Update graph every 150ms
    
    // Screen dimming (power saving)
    unsigned long _lastTouchTime;
    bool _screenDimmed;
    uint8_t _userBrightness;  // User's preferred brightness (10-100%)
    static const unsigned long SCREEN_DIM_TIMEOUT = 60000;  // Dim after 60 seconds (1 minute)
    static const uint8_t SCREEN_DIM_BRIGHTNESS = 25;        // 10% brightness when dimmed (0-255)
    void updateScreenBrightness();
    void wakeScreen();
    void setBrightness(uint8_t percent);  // Set user brightness (10-100%)
    void loadBrightnessFromSPIFFS();
    void saveBrightnessToSPIFFS();
    
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
    static void mode_btn_event(lv_event_t* e);  // Mode switch button
    static void band_prev_event(lv_event_t* e);
    static void band_next_event(lv_event_t* e);
    static void channel_prev_event(lv_event_t* e);
    static void channel_next_event(lv_event_t* e);
    static void threshold_dec_event(lv_event_t* e);
    static void threshold_inc_event(lv_event_t* e);
    static void brightness_slider_event(lv_event_t* e);
    
    // Create UI elements
    void createUI();
    
    // Singleton instance for static callbacks
    static LcdUI* _instance;
};

#endif // ENABLE_LCD_UI
#endif // LCD_UI_H

