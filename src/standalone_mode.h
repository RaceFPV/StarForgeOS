#ifndef STANDALONE_MODE_H
#define STANDALONE_MODE_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h> // Standard ESP32 WebServer (synchronous)
#include <SPIFFS.h>
#include <vector>
#include "timing_core.h" // To interact with timing data
#include "config.h"

#if ENABLE_LCD_UI
#include "lcd_ui.h"
#endif

class StandaloneMode {
public:
    StandaloneMode();
    void begin(TimingCore* timingCore);
    void process();

private:
    WebServer _server;
    TimingCore* _timingCore;
    std::vector<LapData> _laps;
    bool _raceActive = false;
    uint32_t _raceStartTime = 0;
    
    // Web server task
    TaskHandle_t _webTaskHandle;
    static void webServerTask(void* parameter);
    String _apSSID;

#if ENABLE_LCD_UI
    // LCD UI
    LcdUI* _lcdUI;
    TaskHandle_t _lcdTaskHandle;
    
    // LCD button callbacks (called from LCD task, must be thread-safe)
    static void lcdStartCallback();
    static void lcdStopCallback();
    static void lcdClearCallback();
    static StandaloneMode* _lcdInstance;  // For static callbacks
#endif


    void handleRoot();
    void handleGetStatus();
    void handleGetLaps();
    void handleStartRace();
    void handleStopRace();
    void handleClearLaps();
    void handleSetFrequency();
    void handleSetThreshold();
    void handleGetChannels();
    void handleTestHardware();
    void handleTestChannelMode();
    void handleTestChannelModeLow();
    void handleTestChannelModeHigh();
    void handleStyleCSS();
    void handleAppJS();
    void handleNotFound();
    void setupWiFiAP();
};

#endif // STANDALONE_MODE_H