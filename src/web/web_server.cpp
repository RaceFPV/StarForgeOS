#include "web_server.h"
#include "config/config.h"
#include "config/config_globals.h"
#include "settings/settings_manager.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <ArduinoJson.h>

// Helper function to find band/channel from frequency (same lookup as timing_core)
// This ensures band/channel are updated when frequency is set, so they persist correctly
static void findBandChannelFromFrequency(uint16_t freq, uint8_t& band, uint8_t& channel) {
    // Frequency table matching timing_core.cpp
    static const uint16_t freqTable[6][8] = {
        // Band A (Boscam A)
        {5865, 5845, 5825, 5805, 5785, 5765, 5745, 5725},
        // Band B (Boscam B)
        {5733, 5752, 5771, 5790, 5809, 5828, 5847, 5866},
        // Band E (Boscam E / DJI)
        {5705, 5685, 5665, 5645, 5885, 5905, 5925, 5945},
        // Band F (Fatshark / NexWave)
        {5740, 5760, 5780, 5800, 5820, 5840, 5860, 5880},
        // Band R (Raceband)
        {5658, 5695, 5732, 5769, 5806, 5843, 5880, 5917},
        // Band L (Low Race)
        {5362, 5399, 5436, 5473, 5510, 5547, 5584, 5621}
    };

    uint16_t min_diff = 65535;
    band = 0;
    channel = 0;

    // Find closest match in frequency table
    for (uint8_t b = 0; b < 6; b++) {
        for (uint8_t c = 0; c < 8; c++) {
            uint16_t diff = abs((int)freqTable[b][c] - (int)freq);
            if (diff < min_diff) {
                min_diff = diff;
                band = b;
                channel = c;
            }
        }
    }
}

WebServerManager::WebServerManager() : _server(80), _timingCore(nullptr),
    _settingsManager(nullptr), _raceActive(nullptr), 
    _raceStartTime(nullptr), _laps(nullptr)
#if ENABLE_BATTERY_MONITOR && defined(BATTERY_ADC_PIN)
    , _cachedBatteryVoltage(0.0f)
    , _cachedBatteryPercentage(0)
    , _cachedBatteryCharging(false)
    , _batteryDataValid(false)
#endif
    , _webTaskHandle(nullptr)
    , _indexHtmlBuffer(nullptr)
    , _indexHtmlSize(0)
    , _styleCssBuffer(nullptr)
    , _styleCssSize(0)
    , _appJsBuffer(nullptr)
    , _appJsSize(0) {
    // Constructor
}

void WebServerManager::begin(TimingCore* timingCore, SettingsManager* settingsManager,
                              bool* raceActive, uint32_t* raceStartTime, std::vector<LapData>* laps) {
    _timingCore = timingCore;
    _settingsManager = settingsManager;
    _raceActive = raceActive;
    _raceStartTime = raceStartTime;
    _laps = laps;

    // Initialize mDNS for .local hostname
    if (MDNS.begin(MDNS_HOSTNAME)) {
        Serial.printf("mDNS responder started: %s.local\n", MDNS_HOSTNAME);
        MDNS.addService("http", "tcp", WEB_SERVER_PORT);
    } else {
        Serial.println("Warning: Error setting up mDNS responder (not critical)");
    }

    // Initialize SPIFFS for serving static files
    bool spiffsMounted = false;
    if (SPIFFS.begin(false)) {
        spiffsMounted = true;
    } else {
        spiffsMounted = SPIFFS.begin(true);
    }

    if (!spiffsMounted) {
        Serial.println("Warning: SPIFFS Mount Failed (index.html won't be available, but API will work)");
    } else {
        Serial.println("SPIFFS mounted successfully");

        // Get SPIFFS partition info
        size_t totalBytes = SPIFFS.totalBytes();
        size_t usedBytes = SPIFFS.usedBytes();
        Serial.printf("SPIFFS Partition: %d bytes total, %d bytes used, %d bytes free\n",
                     totalBytes, usedBytes, totalBytes - usedBytes);

        // Debug: List all files in SPIFFS
        Serial.println("=== SPIFFS Contents ===");
        File root = SPIFFS.open("/");
        if (!root) {
            Serial.println("ERROR: Failed to open SPIFFS root directory");
        } else if (!root.isDirectory()) {
            Serial.println("ERROR: SPIFFS root is not a directory");
        } else {
            File file = root.openNextFile();
            int fileCount = 0;
            while (file) {
                Serial.printf("  File: %s, Size: %d bytes\n", file.name(), file.size());
                fileCount++;
                file = root.openNextFile();
            }
            if (fileCount == 0) {
                Serial.println("  WARNING: SPIFFS is empty! No files found.");
                Serial.println("  This means SPIFFS was not uploaded correctly or partition is empty.");
            } else {
                Serial.printf("Total files: %d\n", fileCount);
            }
        }
        Serial.println("======================");
    }

    // Pre-load SPIFFS files into DRAM buffers (for ESP32-WROOM-32D cache safety)
    // This must be done BEFORE timing core is activated to avoid cache conflicts
    #if defined(ARDUINO_ESP32_DEV)
    if (spiffsMounted) {
        preloadSpiffsFiles();
    }
    #endif

    // Setup web server routes
    _server.on("/", HTTP_GET, [this]() { handleRoot(); });
    _server.on("/api/status", HTTP_GET, [this]() { handleGetStatus(); });
    _server.on("/api/laps", HTTP_GET, [this]() { handleGetLaps(); });
    _server.on("/api/rssi_history", HTTP_GET, [this]() { handleGetRSSIHistory(); });
    _server.on("/api/start_race", HTTP_POST, [this]() { handleStartRace(); });
    _server.on("/api/stop_race", HTTP_POST, [this]() { handleStopRace(); });
    _server.on("/api/clear_laps", HTTP_POST, [this]() { handleClearLaps(); });
    _server.on("/api/set_frequency", HTTP_POST, [this]() { handleSetFrequency(); });
    _server.on("/api/set_threshold", HTTP_POST, [this]() { handleSetThreshold(); });
    _server.on("/api/get_channels", HTTP_GET, [this]() { handleGetChannels(); });
    _server.on("/api/spiffs_info", HTTP_GET, [this]() { handleGetSPIFFSInfo(); });
    _server.on("/api/config", HTTP_GET, [this]() { handleGetConfig(); });
    _server.on("/style.css", HTTP_GET, [this]() { handleStyleCSS(); });
    _server.on("/app.js", HTTP_GET, [this]() { handleAppJS(); });
    _server.onNotFound([this]() { handleNotFound(); });

    _server.begin();
    Serial.println("Web server started");
    Serial.printf("Access point: WiFi AP\n");
    Serial.printf("IP address: %s\n", WiFi.softAPIP().toString().c_str());
    Serial.printf("mDNS hostname: %s.local\n", MDNS_HOSTNAME);
    Serial.println("Open browser to http://192.168.4.1 or http://sfos.local");
}

void WebServerManager::startTask() {
    // Create dedicated web server task
    // ESP32 dual-core: Pin to Core 0 (same as WiFi stack)
    // ESP32-C3/C6 (single core): No core pinning
#if defined(CONFIG_IDF_TARGET_ESP32C6) || defined(ARDUINO_ESP32C3_DEV) || defined(CONFIG_IDF_TARGET_ESP32C3)
    xTaskCreate(webServerTask, "WebServer", 8192, this, WEB_PRIORITY, &_webTaskHandle);
    Serial.println("Web server task created");
#else
    xTaskCreatePinnedToCore(webServerTask, "WebServer", 8192, this, WEB_PRIORITY, &_webTaskHandle, 0);
    Serial.println("Web server task created on Core 0");
#endif
}

void WebServerManager::handleClient() {
    _server.handleClient();
}

void WebServerManager::webServerTask(void* parameter) {
    WebServerManager* self = static_cast<WebServerManager*>(parameter);
    if (!self) {
        vTaskDelete(nullptr);
        return;
    }

    for (;;) {
        self->_server.handleClient();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void WebServerManager::preloadSpiffsFiles() {
    // Pre-load SPIFFS files into DRAM buffers at startup (before timing core is active)
    // This prevents cache conflicts when serving files at runtime
    Serial.println("Pre-loading SPIFFS files into DRAM buffers...");

    // Pre-load index.html
    if (SPIFFS.exists("/index.html")) {
        File file = SPIFFS.open("/index.html", "r");
        if (file && file.size() > 0) {
            _indexHtmlSize = file.size();
            _indexHtmlBuffer = (char*)heap_caps_malloc(_indexHtmlSize + 1, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            if (_indexHtmlBuffer) {
                file.readBytes(_indexHtmlBuffer, _indexHtmlSize);
                _indexHtmlBuffer[_indexHtmlSize] = '\0';
                Serial.printf("Pre-loaded index.html: %d bytes\n", _indexHtmlSize);
            } else {
                Serial.println("ERROR: Failed to allocate buffer for index.html");
                _indexHtmlSize = 0;
            }
            file.close();
        }
    }

    // Pre-load style.css
    if (SPIFFS.exists("/style.css")) {
        File file = SPIFFS.open("/style.css", "r");
        if (file && file.size() > 0) {
            _styleCssSize = file.size();
            _styleCssBuffer = (char*)heap_caps_malloc(_styleCssSize + 1, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            if (_styleCssBuffer) {
                file.readBytes(_styleCssBuffer, _styleCssSize);
                _styleCssBuffer[_styleCssSize] = '\0';
                Serial.printf("Pre-loaded style.css: %d bytes\n", _styleCssSize);
            } else {
                Serial.println("ERROR: Failed to allocate buffer for style.css");
                _styleCssSize = 0;
            }
            file.close();
        }
    }

    // Pre-load app.js
    if (SPIFFS.exists("/app.js")) {
        File file = SPIFFS.open("/app.js", "r");
        if (file && file.size() > 0) {
            _appJsSize = file.size();
            _appJsBuffer = (char*)heap_caps_malloc(_appJsSize + 1, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            if (_appJsBuffer) {
                file.readBytes(_appJsBuffer, _appJsSize);
                _appJsBuffer[_appJsSize] = '\0';
                Serial.printf("Pre-loaded app.js: %d bytes\n", _appJsSize);
            } else {
                Serial.println("ERROR: Failed to allocate buffer for app.js");
                _appJsSize = 0;
            }
            file.close();
        }
    }

    Serial.println("SPIFFS file pre-loading complete");
}

// ===== HTTP Handlers =====

void WebServerManager::handleRoot() {
    _server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    _server.sendHeader("Pragma", "no-cache");
    _server.sendHeader("Expires", "0");

    #if defined(ARDUINO_ESP32_DEV)
    // ESP32-WROOM-32D: Use pre-loaded buffer (no SPIFFS access at runtime)
    // Send in chunks to avoid socket buffer overflow
    if (_indexHtmlBuffer && _indexHtmlSize > 0) {
        Serial.println("Serving index.html from pre-loaded DRAM buffer");
        _server.setContentLength(_indexHtmlSize);
        _server.send(200, "text/html", "");
        
        // Send content in chunks to avoid buffer overflow
        size_t chunkSize = 1024;  // 1KB chunks
        size_t offset = 0;
        while (offset < _indexHtmlSize) {
            size_t toSend = (_indexHtmlSize - offset > chunkSize) ? chunkSize : (_indexHtmlSize - offset);
            // Create String from buffer chunk (sendContent requires String)
            String chunk(_indexHtmlBuffer + offset, toSend);
            _server.sendContent(chunk);
            offset += toSend;
        }
        return;
    } else {
        Serial.println("ERROR: index.html buffer not available");
        _server.send(500, "text/plain", "index.html not pre-loaded");
        return;
    }
    #else
    // Other boards: Read from SPIFFS directly
    if (!SPIFFS.exists("/index.html")) {
        Serial.println("ERROR: /index.html does not exist in SPIFFS");
        _server.send(404, "text/plain", "index.html not found in SPIFFS");
        return;
    }

    File file = SPIFFS.open("/index.html", "r");
    if (!file) {
        Serial.println("ERROR: Failed to open /index.html");
        _server.send(500, "text/plain", "Failed to read index.html");
        return;
    }

    if (file.size() == 0) {
        Serial.println("ERROR: /index.html exists but is empty");
        file.close();
        _server.send(500, "text/plain", "index.html is empty");
        return;
    }

    Serial.println("Serving index.html from SPIFFS");
    _server.streamFile(file, "text/html");
    file.close();
    #endif
}

void WebServerManager::handleGetStatus() {
    uint8_t current_rssi = _timingCore ? _timingCore->getCurrentRSSI() : 0;
    uint16_t frequency = _timingCore ? _timingCore->getState().frequency_mhz : 5800;
    uint8_t enter_rssi = _timingCore ? _timingCore->getEnterRssi() : 120;
    uint8_t exit_rssi = _timingCore ? _timingCore->getExitRssi() : 100;
    bool crossing = _timingCore ? _timingCore->isCrossing() : false;

    Serial.printf("[API] RSSI: %d, Freq: %d, Enter: %d, Exit: %d, Crossing: %s\n",
                  current_rssi, frequency, enter_rssi, exit_rssi, crossing ? "true" : "false");

    String json = "{";
    json += "\"status\":\"" + String(*_raceActive ? "racing" : "ready") + "\",";
    json += "\"lap_count\":" + String(_laps->size()) + ",";
    json += "\"uptime\":" + String(millis()) + ",";
    json += "\"rssi\":" + String(current_rssi) + ",";
    json += "\"frequency\":" + String(frequency) + ",";
    json += "\"enter_rssi\":" + String(enter_rssi) + ",";
    json += "\"exit_rssi\":" + String(exit_rssi) + ",";
    json += "\"threshold\":" + String(enter_rssi) + ",";  // Backward compatibility
    json += "\"crossing\":" + String(crossing ? "true" : "false");

#if ENABLE_BATTERY_MONITOR && defined(BATTERY_ADC_PIN)
    // Add battery status from cached values (updated via polling in StandaloneMode)
    if (_batteryDataValid) {
        json += ",\"battery\":{";
        json += "\"voltage\":" + String(_cachedBatteryVoltage, 2) + ",";
        json += "\"percentage\":" + String(_cachedBatteryPercentage);
#if defined(USB_DETECT_PIN)
        json += ",\"charging\":" + String(_cachedBatteryCharging ? "true" : "false");
#endif
        json += "}";
    }
#endif

    json += "}";

    Serial.printf("[API] JSON Response: %s\n", json.c_str());
    _server.send(200, "application/json", json);
}

void WebServerManager::handleGetLaps() {
    String json = "[";

    for (size_t i = 0; i < _laps->size(); i++) {
        if (i > 0) json += ",";
        json += "{";
        json += "\"lap_number\":" + String(i + 1) + ",";
        json += "\"timestamp_ms\":" + String((*_laps)[i].timestamp_ms) + ",";
        json += "\"peak_rssi\":" + String((*_laps)[i].rssi_peak) + ",";

        uint32_t lapTime = 0;
        if (i == 0) {
            lapTime = (*_laps)[i].timestamp_ms - *_raceStartTime;
        } else {
            lapTime = (*_laps)[i].timestamp_ms - (*_laps)[i-1].timestamp_ms;
        }
        json += "\"lap_time_ms\":" + String(lapTime);
        json += "}";
    }

    json += "]";
    _server.send(200, "application/json", json);
}

void WebServerManager::handleGetRSSIHistory() {
#if RSSI_HISTORY_ENABLED
    if (!_timingCore) {
        _server.send(500, "application/json", "{\"error\":\"Timing core not available\"}");
        return;
    }

    uint32_t count = _timingCore->getRSSIHistoryCount();

    String json = "{\"count\":" + String(count) + ",\"samples\":[";

    RSSISample sample;
    for (uint32_t i = 0; i < count; i++) {
        if (_timingCore->getRSSIHistorySample(i, sample)) {
            if (i > 0) json += ",";
            json += "{\"t\":" + String(sample.timestamp_ms) + ",\"r\":" + String(sample.rssi) + "}";
        }
    }

    json += "]}";
    _server.send(200, "application/json", json);
#else
    _server.send(501, "application/json", "{\"error\":\"RSSI history not enabled\"}");
#endif
}

void WebServerManager::handleStartRace() {
    *_raceActive = true;
    *_raceStartTime = millis();
    _laps->clear();

    if (_timingCore) {
        while (_timingCore->hasNewLap()) {
            _timingCore->getNextLap();
        }
    }

    Serial.println("Race started!");
    _server.send(200, "application/json", "{\"status\":\"race_started\"}");
}

void WebServerManager::handleStopRace() {
    *_raceActive = false;
    Serial.println("Race stopped!");
    _server.send(200, "application/json", "{\"status\":\"race_stopped\"}");
}

void WebServerManager::handleClearLaps() {
    _laps->clear();
    Serial.println("Laps cleared!");
    _server.send(200, "application/json", "{\"status\":\"laps_cleared\"}");
}

void WebServerManager::handleSetFrequency() {
    if (_server.hasArg("frequency")) {
        int freq = _server.arg("frequency").toInt();
        if (freq >= 5645 && freq <= 5945) {
            if (_timingCore) {
                uint8_t band, channel;
                findBandChannelFromFrequency(freq, band, channel);
                _timingCore->setRX5808Settings(band, channel);
                if (_settingsManager) {
                    _settingsManager->saveSettings(_timingCore);
                }
                Serial.printf("Frequency set to: %d MHz (Band=%d, Channel=%d, saved)\n",
                             freq, band, channel);
            }
            _server.send(200, "application/json", "{\"status\":\"frequency_set\",\"frequency\":" + String(freq) + "}");
        } else {
            _server.send(400, "application/json", "{\"error\":\"invalid_frequency\"}");
        }
    } else {
        _server.send(400, "application/json", "{\"error\":\"missing_frequency\"}");
    }
}

void WebServerManager::handleSetThreshold() {
    if (_server.hasArg("enter_rssi") && _server.hasArg("exit_rssi")) {
        int enter_rssi = _server.arg("enter_rssi").toInt();
        int exit_rssi = _server.arg("exit_rssi").toInt();
        if (enter_rssi >= 0 && enter_rssi <= 255 && exit_rssi >= 0 && exit_rssi <= 255 && enter_rssi > exit_rssi) {
            if (_timingCore) {
                _timingCore->setEnterRssi(enter_rssi);
                _timingCore->setExitRssi(exit_rssi);
                if (_settingsManager) {
                    _settingsManager->saveSettings(_timingCore);
                }
            }
            _server.send(200, "application/json",
                "{\"status\":\"threshold_set\",\"enter_rssi\":" + String(enter_rssi) +
                ",\"exit_rssi\":" + String(exit_rssi) + "}");
            Serial.printf("Thresholds set: Enter=%d, Exit=%d (saved)\n", enter_rssi, exit_rssi);
        } else {
            _server.send(400, "application/json", "{\"error\":\"invalid_threshold\"}");
        }
    } else if (_server.hasArg("threshold")) {
        int threshold = _server.arg("threshold").toInt();
        if (threshold >= 0 && threshold <= 255) {
            uint8_t enter = threshold;
            uint8_t exit = (threshold > 20) ? (threshold - 20) : threshold;

            if (_timingCore) {
                _timingCore->setEnterRssi(enter);
                _timingCore->setExitRssi(exit);
                if (_settingsManager) {
                    _settingsManager->saveSettings(_timingCore);
                }
            }
            _server.send(200, "application/json", "{\"status\":\"threshold_set\",\"threshold\":" + String(threshold) + "}");
            Serial.printf("Threshold set to: %d (migrated to Enter=%d, Exit=%d, saved)\n", threshold, enter, exit);
        } else {
            _server.send(400, "application/json", "{\"error\":\"invalid_threshold\"}");
        }
    } else {
        _server.send(400, "application/json", "{\"error\":\"missing_threshold\"}");
    }
}

void WebServerManager::handleGetChannels() {
    String json = "{";
    json += "\"bands\":{";

    // Raceband (R1-R8)
    json += "\"Raceband\":[";
    int raceband[] = {5658, 5695, 5732, 5769, 5806, 5843, 5880, 5917};
    for (int i = 0; i < 8; i++) {
        if (i > 0) json += ",";
        json += "{\"channel\":\"R" + String(i+1) + "\",\"frequency\":" + String(raceband[i]) + "}";
    }
    json += "],";

    // Fatshark (F1-F8)
    json += "\"Fatshark\":[";
    int fatshark[] = {5740, 5760, 5780, 5800, 5820, 5840, 5860, 5880};
    for (int i = 0; i < 8; i++) {
        if (i > 0) json += ",";
        json += "{\"channel\":\"F" + String(i+1) + "\",\"frequency\":" + String(fatshark[i]) + "}";
    }
    json += "],";

    // Boscam A (A1-A8)
    json += "\"Boscam_A\":[";
    int boscam_a[] = {5865, 5845, 5825, 5805, 5785, 5765, 5745, 5725};
    for (int i = 0; i < 8; i++) {
        if (i > 0) json += ",";
        json += "{\"channel\":\"A" + String(i+1) + "\",\"frequency\":" + String(boscam_a[i]) + "}";
    }
    json += "],";

    // Boscam E (E1-E8)
    json += "\"Boscam_E\":[";
    int boscam_e[] = {5705, 5685, 5665, 5645, 5885, 5905, 5925, 5945};
    for (int i = 0; i < 8; i++) {
        if (i > 0) json += ",";
        json += "{\"channel\":\"E" + String(i+1) + "\",\"frequency\":" + String(boscam_e[i]) + "}";
    }
    json += "]";

    json += "}}";

    _server.send(200, "application/json", json);
}

void WebServerManager::handleGetSPIFFSInfo() {
    String json = "{";
    json += "\"mounted\":";
    json += SPIFFS.begin(false) ? "true" : "false";
    json += ",";

    if (SPIFFS.begin(false)) {
        size_t totalBytes = SPIFFS.totalBytes();
        size_t usedBytes = SPIFFS.usedBytes();
        json += "\"total_bytes\":" + String(totalBytes) + ",";
        json += "\"used_bytes\":" + String(usedBytes) + ",";
        json += "\"free_bytes\":" + String(totalBytes - usedBytes) + ",";

        json += "\"files\":[";
        File root = SPIFFS.open("/");
        if (root && root.isDirectory()) {
            File file = root.openNextFile();
            bool first = true;
            while (file) {
                if (!first) json += ",";
                json += "{";
                json += "\"name\":\"" + String(file.name()) + "\",";
                json += "\"size\":" + String(file.size());
                json += "}";
                first = false;
                file = root.openNextFile();
            }
        }
        json += "]";
    } else {
        json += "\"error\":\"SPIFFS not mounted\"";
    }

    json += "}";
    _server.send(200, "application/json", json);
}

void WebServerManager::handleGetConfig() {
    Preferences prefs;

    if (!prefs.begin("sfos_pins", true)) {
        Serial.println("API: Failed to open NVS for reading pin config");
        _server.send(404, "application/json", "{\"error\":\"Pin config not found in NVS\",\"exists\":false}");
        return;
    }

    uint8_t enabled = prefs.getUChar("pin_enabled", 0);
    if (enabled == 0) {
        prefs.end();
        Serial.println("API: Custom pin config not enabled in NVS");
        _server.send(404, "application/json", "{\"error\":\"Custom pin config not enabled\",\"exists\":false}");
        return;
    }

    DynamicJsonDocument configDoc(1024);
    JsonObject customPins = configDoc.createNestedObject("custom_pins");
    customPins["enabled"] = true;

    customPins["rssi_input"] = prefs.getUChar("pin_rssi_input", 0);
    customPins["rx5808_data"] = prefs.getUChar("pin_rx5808_data", 0);
    customPins["rx5808_clk"] = prefs.getUChar("pin_rx5808_clk", 0);
    customPins["rx5808_sel"] = prefs.getUChar("pin_rx5808_sel", 0);
    customPins["mode_switch"] = prefs.getUChar("pin_mode_switch", 0);

    uint8_t power_button = prefs.getUChar("pin_power_button", 0);
    if (power_button > 0 || prefs.isKey("pin_power_button")) {
        customPins["power_button"] = power_button;
    }
    uint8_t battery_adc = prefs.getUChar("pin_battery_adc", 0);
    if (battery_adc > 0 || prefs.isKey("pin_battery_adc")) {
        customPins["battery_adc"] = battery_adc;
    }
    uint8_t audio_dac = prefs.getUChar("pin_audio_dac", 0);
    if (audio_dac > 0 || prefs.isKey("pin_audio_dac")) {
        customPins["audio_dac"] = audio_dac;
    }
    uint8_t usb_detect = prefs.getUChar("pin_usb_detect", 0);
    if (usb_detect > 0 || prefs.isKey("pin_usb_detect")) {
        customPins["usb_detect"] = usb_detect;
    }

    if (prefs.isKey("pin_lcd_i2c_sda")) {
        customPins["lcd_i2c_sda"] = prefs.getChar("pin_lcd_i2c_sda", -1);
    }
    if (prefs.isKey("pin_lcd_i2c_scl")) {
        customPins["lcd_i2c_scl"] = prefs.getChar("pin_lcd_i2c_scl", -1);
    }
    if (prefs.isKey("pin_lcd_backlight")) {
        customPins["lcd_backlight"] = prefs.getChar("pin_lcd_backlight", -1);
    }

    prefs.end();

    DynamicJsonDocument responseDoc(1536);
    responseDoc["exists"] = true;
    responseDoc["source"] = "NVS";
    responseDoc["content"] = configDoc;

    String response;
    serializeJson(responseDoc, response);

    Serial.println("API: Serving pin config from NVS");
    _server.send(200, "application/json", response);
}

void WebServerManager::handleStyleCSS() {
    _server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    _server.sendHeader("Pragma", "no-cache");
    _server.sendHeader("Expires", "0");

    #if defined(ARDUINO_ESP32_DEV)
    // ESP32-WROOM-32D: Use pre-loaded buffer (no SPIFFS access at runtime)
    // Send in chunks to avoid socket buffer overflow
    if (_styleCssBuffer && _styleCssSize > 0) {
        Serial.println("Serving style.css from pre-loaded DRAM buffer");
        _server.setContentLength(_styleCssSize);
        _server.send(200, "text/css", "");
        
        // Send content in chunks to avoid buffer overflow
        size_t chunkSize = 1024;  // 1KB chunks
        size_t offset = 0;
        while (offset < _styleCssSize) {
            size_t toSend = (_styleCssSize - offset > chunkSize) ? chunkSize : (_styleCssSize - offset);
            // Create String from buffer chunk (sendContent requires String)
            String chunk(_styleCssBuffer + offset, toSend);
            _server.sendContent(chunk);
            offset += toSend;
        }
        return;
    } else {
        Serial.println("ERROR: style.css buffer not available");
        _server.send(500, "text/plain", "style.css not pre-loaded");
        return;
    }
    #else
    // Other boards: Read from SPIFFS directly
    File file = SPIFFS.open("/style.css", "r");
    if (file && file.size() > 0) {
        Serial.println("Serving style.css from SPIFFS");
        _server.streamFile(file, "text/css");
        file.close();
        return;
    }
    #endif

    Serial.println("ERROR: style.css not found in SPIFFS!");
    Serial.println("Please run 'pio run -t uploadfs' to upload web files");

    _server.send(200, "text/css",
        "body{font-family:Arial,sans-serif;background:#1a1f35;color:#fff;padding:40px;text-align:center;}"
        "h1{color:#ff7b00;margin-bottom:20px;}"
        ".error{background:#2a0f0f;border:2px solid #ff3838;border-radius:8px;padding:30px;max-width:600px;margin:0 auto;}"
    );
}

void WebServerManager::handleAppJS() {
    _server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    _server.sendHeader("Pragma", "no-cache");
    _server.sendHeader("Expires", "0");

    #if defined(ARDUINO_ESP32_DEV)
    // ESP32-WROOM-32D: Use pre-loaded buffer (no SPIFFS access at runtime)
    // Send in chunks to avoid socket buffer overflow
    if (_appJsBuffer && _appJsSize > 0) {
        Serial.println("Serving app.js from pre-loaded DRAM buffer");
        _server.setContentLength(_appJsSize);
        _server.send(200, "application/javascript", "");
        
        // Send content in chunks to avoid buffer overflow
        size_t chunkSize = 1024;  // 1KB chunks
        size_t offset = 0;
        while (offset < _appJsSize) {
            size_t toSend = (_appJsSize - offset > chunkSize) ? chunkSize : (_appJsSize - offset);
            // Create String from buffer chunk (sendContent requires String)
            String chunk(_appJsBuffer + offset, toSend);
            _server.sendContent(chunk);
            offset += toSend;
        }
        return;
    } else {
        Serial.println("ERROR: app.js buffer not available");
        _server.send(500, "text/plain", "app.js not pre-loaded");
        return;
    }
    #else
    // Other boards: Read from SPIFFS directly
    File file = SPIFFS.open("/app.js", "r");
    if (file && file.size() > 0) {
        Serial.println("Serving app.js from SPIFFS");
        _server.streamFile(file, "application/javascript");
        file.close();
        return;
    }
    #endif

    Serial.println("ERROR: app.js not found in SPIFFS!");
    Serial.println("Please run 'pio run -t uploadfs' to upload web files");

    _server.send(200, "application/javascript",
        "console.error('app.js not found in SPIFFS - Please upload filesystem');"
        "document.body.innerHTML='<div class=\"error\"><h1>⚠️ Files Missing</h1>"
        "<p>Web interface files not found on device.</p>"
        "<p>Please run: <code>pio run -t uploadfs</code></p></div>';"
    );
}

void WebServerManager::handleNotFound() {
    _server.send(404, "text/plain", "File not found");
}

#if ENABLE_BATTERY_MONITOR && defined(BATTERY_ADC_PIN)
void WebServerManager::updateBatteryStatus(float voltage, uint8_t percentage, bool isCharging) {
    // Cache battery data for serving via web API (called from StandaloneMode polling loop)
    _cachedBatteryVoltage = voltage;
    _cachedBatteryPercentage = percentage;
    _cachedBatteryCharging = isCharging;
    _batteryDataValid = true;
}
#endif
