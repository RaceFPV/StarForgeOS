#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <SPIFFS.h>
#include <ESPAsyncWebServer.h>
#include <vector>
#include "timing_core.h"

// Forward declarations
class SettingsManager;

// Web server manager for standalone mode
// Handles HTTP server setup, routing, and API endpoints using ESPAsyncWebServer
class WebServerManager {
public:
    WebServerManager();

    // Initialize web server with routes and start listening
    // Must be called after WiFi AP is up
    void begin(TimingCore* timingCore, SettingsManager* settingsManager,
               bool* raceActive, uint32_t* raceStartTime, std::vector<LapData>* laps);

#if ENABLE_BATTERY_MONITOR && defined(BATTERY_ADC_PIN)
    // Update battery status (called from StandaloneMode polling loop)
    // This caches battery data so it can be served without reading on-demand
    void updateBatteryStatus(float voltage, uint8_t percentage, bool isCharging = false);
#endif

private:
    AsyncWebServer _server;
    TimingCore* _timingCore;
    SettingsManager* _settingsManager;

    // Race state (shared with StandaloneMode)
    bool* _raceActive;
    uint32_t* _raceStartTime;
    std::vector<LapData>* _laps;

#if ENABLE_BATTERY_MONITOR && defined(BATTERY_ADC_PIN)
    // Battery monitoring - cached values (updated via polling in StandaloneMode)
    float _cachedBatteryVoltage;
    uint8_t _cachedBatteryPercentage;
    bool _cachedBatteryCharging;
    bool _batteryDataValid;  // True when battery data has been read at least once
#endif

    // HTTP handlers (AsyncWebServer uses callbacks with AsyncWebServerRequest*)
    void handleRoot(AsyncWebServerRequest* request);
    void handleGetStatus(AsyncWebServerRequest* request);
    void handleGetLaps(AsyncWebServerRequest* request);
    void handleGetRSSIHistory(AsyncWebServerRequest* request);
    void handleStartRace(AsyncWebServerRequest* request);
    void handleStopRace(AsyncWebServerRequest* request);
    void handleClearLaps(AsyncWebServerRequest* request);
    void handleSetFrequency(AsyncWebServerRequest* request);
    void handleSetThreshold(AsyncWebServerRequest* request);
    void handleGetChannels(AsyncWebServerRequest* request);
    void handleGetSPIFFSInfo(AsyncWebServerRequest* request);
    void handleGetConfig(AsyncWebServerRequest* request);
    void handleStyleCSS(AsyncWebServerRequest* request);
    void handleAppJS(AsyncWebServerRequest* request);
    void handleNotFound(AsyncWebServerRequest* request);
};

#endif // WEB_SERVER_H
