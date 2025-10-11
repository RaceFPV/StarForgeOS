#ifndef STANDALONE_MODE_H
#define STANDALONE_MODE_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h> // Standard ESP32 WebServer (synchronous)
#include <SPIFFS.h>
#include <vector>
#include "timing_core.h" // To interact with timing data
#include "config.h"

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

    void handleRoot();
    void handleGetStatus();
    void handleGetLaps();
    void handleStartRace();
    void handleStopRace();
    void handleClearLaps();
    void handleSetFrequency();
    void handleSetThreshold();
    void handleGetChannels();
    void handleStyleCSS();
    void handleAppJS();
    void handleNotFound();
    void setupWiFiAP();
};

#endif // STANDALONE_MODE_H