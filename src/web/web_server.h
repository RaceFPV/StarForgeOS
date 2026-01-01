#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <FS.h>  // Include FS.h first to ensure proper namespace resolution
using fs::FS;    // Bring FS into global namespace for WebServer.h compatibility
#include <WebServer.h>
#include <SPIFFS.h>
#include <vector>
#include "timing_core.h"

// Forward declarations
class SettingsManager;

// Web server manager for standalone mode
// Handles HTTP server setup, routing, and API endpoints
class WebServerManager {
public:
    WebServerManager();

    // Initialize web server with routes and start listening
    // Must be called after WiFi AP is up
    void begin(TimingCore* timingCore, SettingsManager* settingsManager,
               bool* raceActive, uint32_t* raceStartTime, std::vector<LapData>* laps);

    // Handle incoming client requests (call from task loop)
    void handleClient();

    // Start dedicated web server task
    void startTask();

#if ENABLE_BATTERY_MONITOR && defined(BATTERY_ADC_PIN)
    // Update battery status (called from StandaloneMode polling loop)
    // This caches battery data so it can be served without reading on-demand
    void updateBatteryStatus(float voltage, uint8_t percentage, bool isCharging = false);
#endif

private:
    WebServer _server;
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

    TaskHandle_t _webTaskHandle;

    // Pre-loaded SPIFFS file buffers (for ESP32-WROOM-32D cache safety)
    // Files are loaded at startup before timing core is active
    char* _indexHtmlBuffer;
    size_t _indexHtmlSize;
    char* _styleCssBuffer;
    size_t _styleCssSize;
    char* _appJsBuffer;
    size_t _appJsSize;

    // Pre-load SPIFFS files into DRAM buffers (called at startup)
    void preloadSpiffsFiles();

    // Static task wrapper
    static void webServerTask(void* parameter);

    // HTTP handlers
    void handleRoot();
    void handleGetStatus();
    void handleGetLaps();
    void handleGetRSSIHistory();
    void handleStartRace();
    void handleStopRace();
    void handleClearLaps();
    void handleSetFrequency();
    void handleSetThreshold();
    void handleGetChannels();
    void handleGetSPIFFSInfo();
    void handleGetConfig();
    void handleStyleCSS();
    void handleAppJS();
    void handleNotFound();
};

#endif // WEB_SERVER_H
