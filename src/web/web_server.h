#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "platform/hal.h"
#include "platform/http_request.h"
#include "esp_http_server.h"
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

    void handleRoot(HttpRequest* request);
    void handleGetStatus(HttpRequest* request);
    void handleGetLaps(HttpRequest* request);
    void handleGetRSSIHistory(HttpRequest* request);
    void handleStartRace(HttpRequest* request);
    void handleStopRace(HttpRequest* request);
    void handleClearLaps(HttpRequest* request);
    void handleSetFrequency(HttpRequest* request);
    void handleSetThreshold(HttpRequest* request);
    void handleGetChannels(HttpRequest* request);
    void handleGetSPIFFSInfo(HttpRequest* request);
    void handleGetConfig(HttpRequest* request);
    void handleStyleCSS(HttpRequest* request);
    void handleAppJS(HttpRequest* request);
    void handleNotFound(HttpRequest* request);

#if ENABLE_BATTERY_MONITOR && defined(BATTERY_ADC_PIN)
    // Update battery status (called from StandaloneMode polling loop)
    // This caches battery data so it can be served without reading on-demand
    void updateBatteryStatus(float voltage, uint8_t percentage, bool isCharging = false);
#endif

private:
    httpd_handle_t _server;
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

    void sendStoredFile(HttpRequest* request, const char* web_path, const char* content_type,
                        const char* cache_control, uint32_t pause_ms);
};

#endif // WEB_SERVER_H
