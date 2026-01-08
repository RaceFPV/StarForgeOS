#include "web_server.h"
#include "config/config.h"
#include "config/config_globals.h"
#include "settings/settings_manager.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <cstdio>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Buffer sizes for JSON responses (with safety margins)
#define JSON_STATUS_BUFFER_SIZE 512      // Status response (most frequent)
#define JSON_LAPS_BUFFER_SIZE 16384       // Laps response (100 laps max: ~160 bytes each)
#define JSON_RSSI_BUFFER_SIZE 32768       // RSSI history (30K samples max: ~20 bytes each)
#define JSON_CHANNELS_BUFFER_SIZE 2048    // Channels response (static data)
#define JSON_SPIFFS_BUFFER_SIZE 2048     // SPIFFS info response
#define JSON_CONFIG_BUFFER_SIZE 2048      // Config response

// Helper structure for delayed timing resume (non-blocking)
struct TimingResumeData {
    TimingCore* timingCore;
    bool wasActive;
    uint32_t delayMs;
};

// FreeRTOS task to resume timing after delay (non-blocking, prevents watchdog timeout)
void resumeTimingAfterDelay(void* parameter) {
    TimingResumeData* data = static_cast<TimingResumeData*>(parameter);
    if (data && data->timingCore && data->wasActive) {
        // Wait for the calculated delay
        vTaskDelay(pdMS_TO_TICKS(data->delayMs));
        // Resume timing
        data->timingCore->resumeFromPause(data->wasActive);
    }
    // Free the allocated structure
    delete data;
    // Delete this one-shot task
    vTaskDelete(NULL);
}

// Helper function to schedule delayed timing resume (non-blocking)
static void scheduleTimingResume(TimingCore* timingCore, bool wasActive, uint32_t delayMs) {
    if (!timingCore || !wasActive) return;  // Nothing to resume
    
    TimingResumeData* data = new TimingResumeData;
    data->timingCore = timingCore;
    data->wasActive = wasActive;
    data->delayMs = delayMs;
    
    // Create a one-shot task that will resume timing after delay
    // Low priority (1) so it doesn't interfere with TCP/IP stack
    xTaskCreate(
        resumeTimingAfterDelay,
        "TimingResume",
        2048,  // Stack size
        data,
        1,      // Low priority (below timing core)
        NULL    // Don't need handle
    );
}

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
{
    // Constructor
}

void WebServerManager::begin(TimingCore* timingCore, SettingsManager* settingsManager,
                              bool* raceActive, uint32_t* raceStartTime, std::vector<LapData>* laps) {
    _timingCore = timingCore;
    _settingsManager = settingsManager;
    _raceActive = raceActive;
    _raceStartTime = raceStartTime;
    _laps = laps;

    // CRITICAL: Wait for WiFi to be fully ready before initializing web server
    // ESPAsyncWebServer requires TCP/IP stack to be initialized
    Serial.println("Waiting for WiFi TCP/IP stack to be ready...");
    int wifiWaitCount = 0;
    const int maxWait = 100; // 10 seconds max wait
    
    // For AP mode, wait for AP IP to be assigned (indicates TCP/IP stack is ready)
    if (WiFi.getMode() & WIFI_AP) {
        while (WiFi.softAPIP().toString() == "0.0.0.0" && wifiWaitCount < maxWait) {
            delay(100);
            wifiWaitCount++;
        }
        if (WiFi.softAPIP().toString() != "0.0.0.0") {
            Serial.printf("WiFi AP ready: %s\n", WiFi.softAPIP().toString().c_str());
        } else {
            Serial.println("WARNING: WiFi AP IP not assigned after 10 seconds!");
        }
    } else {
        // For STA mode, wait for connection
        while (WiFi.status() != WL_CONNECTED && wifiWaitCount < maxWait) {
            delay(100);
            wifiWaitCount++;
        }
        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("WiFi STA ready: %s\n", WiFi.localIP().toString().c_str());
        } else {
            Serial.println("WARNING: WiFi STA not connected after 10 seconds!");
        }
    }
    
    // Additional small delay to ensure TCP/IP stack is fully initialized
    delay(200);

    // Initialize mDNS for .local hostname (after WiFi is ready)
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

    // CRITICAL: Additional delay to ensure TCP/IP task is fully ready
    // The TCP/IP stack needs time to initialize its internal structures
    Serial.println("Waiting for TCP/IP stack to be fully ready...");
    delay(500);  // Give TCP/IP task time to initialize

    // Setup web server routes (ESPAsyncWebServer uses lambda callbacks)
    // Route registration must happen AFTER TCP/IP stack is ready
    Serial.println("Registering web server routes...");
    
    // Test route to verify server is working
    _server.on("/test", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/plain", "Web server is working!");
    });
    
    _server.on("/", HTTP_GET, [this](AsyncWebServerRequest* request) { handleRoot(request); });
    _server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* request) { handleGetStatus(request); });
    _server.on("/api/laps", HTTP_GET, [this](AsyncWebServerRequest* request) { handleGetLaps(request); });
    _server.on("/api/rssi_history", HTTP_GET, [this](AsyncWebServerRequest* request) { handleGetRSSIHistory(request); });
    _server.on("/api/start_race", HTTP_POST, [this](AsyncWebServerRequest* request) { handleStartRace(request); });
    _server.on("/api/stop_race", HTTP_POST, [this](AsyncWebServerRequest* request) { handleStopRace(request); });
    _server.on("/api/clear_laps", HTTP_POST, [this](AsyncWebServerRequest* request) { handleClearLaps(request); });
    _server.on("/api/set_frequency", HTTP_POST, [this](AsyncWebServerRequest* request) { handleSetFrequency(request); });
    _server.on("/api/set_threshold", HTTP_POST, [this](AsyncWebServerRequest* request) { handleSetThreshold(request); });
    _server.on("/api/get_channels", HTTP_GET, [this](AsyncWebServerRequest* request) { handleGetChannels(request); });
    _server.on("/api/spiffs_info", HTTP_GET, [this](AsyncWebServerRequest* request) { handleGetSPIFFSInfo(request); });
    _server.on("/api/config", HTTP_GET, [this](AsyncWebServerRequest* request) { handleGetConfig(request); });
    _server.on("/style.css", HTTP_GET, [this](AsyncWebServerRequest* request) { handleStyleCSS(request); });
    _server.on("/app.js", HTTP_GET, [this](AsyncWebServerRequest* request) { handleAppJS(request); });
    _server.onNotFound([this](AsyncWebServerRequest* request) { handleNotFound(request); });

    Serial.println("Starting web server...");
    
    // Verify WiFi is still ready before starting server
    if (WiFi.getMode() & WIFI_AP) {
        IPAddress apIP = WiFi.softAPIP();
        if (apIP.toString() == "0.0.0.0") {
            Serial.println("ERROR: WiFi AP IP not assigned! Cannot start web server.");
            return;
        }
        Serial.printf("WiFi AP IP confirmed: %s\n", apIP.toString().c_str());
    }
    
    // Configure server with longer timeouts for large file transfers
    // This helps prevent NS_ERROR_NET_PARTIAL_TRANSFER errors
    _server.begin();
    
    // Note: ESPAsyncWebServer doesn't expose timeout configuration directly,
    // but AsyncTCP (which it uses) respects system TCP settings configured in platformio.ini
    
    Serial.println("Web server started (ESPAsyncWebServer)");
    Serial.printf("Access point: WiFi AP\n");
    Serial.printf("IP address: %s\n", WiFi.softAPIP().toString().c_str());
    Serial.printf("mDNS hostname: %s.local\n", MDNS_HOSTNAME);
    Serial.printf("Server listening on port 80\n");
    Serial.println("Open browser to http://192.168.4.1 or http://sfos.local");
}


// ===== HTTP Handlers =====

void WebServerManager::handleRoot(AsyncWebServerRequest* request) {
    // ESPAsyncWebServer handles SPIFFS file serving efficiently with automatic chunking
    // Don't open file twice - let ESPAsyncWebServer handle it internally to avoid conflicts
    if (SPIFFS.exists("/index.html")) {
        AsyncWebServerResponse* response = request->beginResponse(SPIFFS, "/index.html", "text/html");
        // HTML should be revalidated (not cached long) since it contains versioned script/style links
        response->addHeader("Cache-Control", "public, max-age=300, must-revalidate");  // 5 min cache, then revalidate
        response->addHeader("ETag", "\"index-v5\"");  // Version tag for cache validation
        request->send(response);
    } else {
        request->send(404, "text/plain", "index.html not found");
    }
}

void WebServerManager::handleGetStatus(AsyncWebServerRequest* request) {
    // Pre-allocated buffer for status JSON (most frequently called handler)
    static char jsonBuffer[JSON_STATUS_BUFFER_SIZE];
    
    uint8_t current_rssi = _timingCore ? _timingCore->getCurrentRSSI() : 0;
    uint16_t frequency = _timingCore ? _timingCore->getState().frequency_mhz : 5800;
    uint8_t enter_rssi = _timingCore ? _timingCore->getEnterRssi() : 120;
    uint8_t exit_rssi = _timingCore ? _timingCore->getExitRssi() : 100;
    bool crossing = _timingCore ? _timingCore->isCrossing() : false;
    size_t lapCount = _laps ? _laps->size() : 0;
    uint32_t uptime = millis();

    // Build JSON using snprintf (bounds-checked, efficient)
    int len = snprintf(jsonBuffer, JSON_STATUS_BUFFER_SIZE,
        "{\"status\":\"%s\",\"lap_count\":%zu,\"uptime\":%lu,\"rssi\":%u,\"frequency\":%u,"
        "\"enter_rssi\":%u,\"exit_rssi\":%u,\"threshold\":%u,\"crossing\":%s",
        (*_raceActive ? "racing" : "ready"), lapCount, uptime, current_rssi, frequency,
        enter_rssi, exit_rssi, enter_rssi, crossing ? "true" : "false");

#if ENABLE_BATTERY_MONITOR && defined(BATTERY_ADC_PIN)
    // Add battery status if available
    if (_batteryDataValid && len > 0 && len < (int)(JSON_STATUS_BUFFER_SIZE - 100)) {
#if defined(USB_DETECT_PIN)
        len += snprintf(jsonBuffer + len, JSON_STATUS_BUFFER_SIZE - len,
            ",\"battery\":{\"voltage\":%.2f,\"percentage\":%u,\"charging\":%s}",
            _cachedBatteryVoltage, _cachedBatteryPercentage, _cachedBatteryCharging ? "true" : "false");
#else
        len += snprintf(jsonBuffer + len, JSON_STATUS_BUFFER_SIZE - len,
            ",\"battery\":{\"voltage\":%.2f,\"percentage\":%u}",
            _cachedBatteryVoltage, _cachedBatteryPercentage);
#endif
    }
#endif

    // Close JSON and verify buffer didn't overflow
    if (len > 0 && len < (int)JSON_STATUS_BUFFER_SIZE - 2) {
        jsonBuffer[len++] = '}';
        jsonBuffer[len] = '\0';
        request->send(200, "application/json", jsonBuffer);
    } else {
        // Buffer overflow protection
        request->send(500, "application/json", "{\"error\":\"response_too_large\"}");
    }
}

void WebServerManager::handleGetLaps(AsyncWebServerRequest* request) {
    if (!_laps || !_raceStartTime) {
        request->send(500, "application/json", "{\"error\":\"invalid_state\"}");
        return;
    }

    // Pre-allocated buffer for laps JSON (MAX_LAPS_STORED = 100)
    static char jsonBuffer[JSON_LAPS_BUFFER_SIZE];
    int len = 0;
    
    // Limit to MAX_LAPS_STORED to prevent buffer overflow
    size_t lapCount = _laps->size();
    if (lapCount > MAX_LAPS_STORED) {
        lapCount = MAX_LAPS_STORED;
    }

    len = snprintf(jsonBuffer, JSON_LAPS_BUFFER_SIZE, "[");
    if (len < 0 || len >= (int)JSON_LAPS_BUFFER_SIZE) {
        request->send(500, "application/json", "{\"error\":\"buffer_overflow\"}");
        return;
    }

    for (size_t i = 0; i < lapCount; i++) {
        if (i > 0) {
            len += snprintf(jsonBuffer + len, JSON_LAPS_BUFFER_SIZE - len, ",");
            if (len < 0 || len >= (int)JSON_LAPS_BUFFER_SIZE) break;
        }

        uint32_t lapTime = 0;
        if (i == 0) {
            lapTime = (*_laps)[i].timestamp_ms - *_raceStartTime;
        } else {
            lapTime = (*_laps)[i].timestamp_ms - (*_laps)[i-1].timestamp_ms;
        }

        int written = snprintf(jsonBuffer + len, JSON_LAPS_BUFFER_SIZE - len,
            "{\"lap_number\":%zu,\"timestamp_ms\":%lu,\"peak_rssi\":%u,\"lap_time_ms\":%lu}",
            i + 1, (*_laps)[i].timestamp_ms, (*_laps)[i].rssi_peak, lapTime);
        
        if (written < 0 || written >= (int)(JSON_LAPS_BUFFER_SIZE - len)) {
            // Buffer overflow - truncate response
            break;
        }
        len += written;
    }

    // Close JSON array
    if (len > 0 && len < (int)JSON_LAPS_BUFFER_SIZE - 2) {
        len += snprintf(jsonBuffer + len, JSON_LAPS_BUFFER_SIZE - len, "]");
        if (len > 0 && len < (int)JSON_LAPS_BUFFER_SIZE) {
            request->send(200, "application/json", jsonBuffer);
            return;
        }
    }

    // Fallback on error
    request->send(500, "application/json", "{\"error\":\"response_too_large\"}");
}

void WebServerManager::handleGetRSSIHistory(AsyncWebServerRequest* request) {
#if RSSI_HISTORY_ENABLED
    if (!_timingCore) {
        request->send(500, "application/json", "{\"error\":\"Timing core not available\"}");
        return;
    }

    uint32_t count = _timingCore->getRSSIHistoryCount();
    
    // Limit response size to prevent buffer overflow (estimate ~20 bytes per sample)
    // Allow up to ~1500 samples to fit in buffer (30KB / 20 bytes)
    const uint32_t MAX_SAMPLES_IN_RESPONSE = (JSON_RSSI_BUFFER_SIZE - 100) / 20;
    if (count > MAX_SAMPLES_IN_RESPONSE) {
        count = MAX_SAMPLES_IN_RESPONSE;
    }

    static char jsonBuffer[JSON_RSSI_BUFFER_SIZE];
    int len = snprintf(jsonBuffer, JSON_RSSI_BUFFER_SIZE, "{\"count\":%lu,\"samples\":[", count);
    
    if (len < 0 || len >= (int)JSON_RSSI_BUFFER_SIZE) {
        request->send(500, "application/json", "{\"error\":\"buffer_overflow\"}");
        return;
    }

    RSSISample sample;
    uint32_t samplesAdded = 0;
    for (uint32_t i = 0; i < count && samplesAdded < MAX_SAMPLES_IN_RESPONSE; i++) {
        if (_timingCore->getRSSIHistorySample(i, sample)) {
            if (samplesAdded > 0) {
                len += snprintf(jsonBuffer + len, JSON_RSSI_BUFFER_SIZE - len, ",");
                if (len < 0 || len >= (int)JSON_RSSI_BUFFER_SIZE) break;
            }
            
            int written = snprintf(jsonBuffer + len, JSON_RSSI_BUFFER_SIZE - len,
                "{\"t\":%lu,\"r\":%u}", sample.timestamp_ms, sample.rssi);
            
            if (written < 0 || written >= (int)(JSON_RSSI_BUFFER_SIZE - len)) {
                break;  // Buffer overflow
            }
            len += written;
            samplesAdded++;
        }
    }

    // Close JSON
    if (len > 0 && len < (int)JSON_RSSI_BUFFER_SIZE - 3) {
        len += snprintf(jsonBuffer + len, JSON_RSSI_BUFFER_SIZE - len, "]}");
        if (len > 0 && len < (int)JSON_RSSI_BUFFER_SIZE) {
            request->send(200, "application/json", jsonBuffer);
            return;
        }
    }

    request->send(500, "application/json", "{\"error\":\"response_too_large\"}");
#else
    request->send(501, "application/json", "{\"error\":\"RSSI history not enabled\"}");
#endif
}

void WebServerManager::handleStartRace(AsyncWebServerRequest* request) {
    *_raceActive = true;
    *_raceStartTime = millis();
    _laps->clear();

    if (_timingCore) {
        while (_timingCore->hasNewLap()) {
            _timingCore->getNextLap();
        }
    }

    Serial.println("Race started!");
    request->send(200, "application/json", "{\"status\":\"race_started\"}");
}

void WebServerManager::handleStopRace(AsyncWebServerRequest* request) {
    *_raceActive = false;
    Serial.println("Race stopped!");
    request->send(200, "application/json", "{\"status\":\"race_stopped\"}");
}

void WebServerManager::handleClearLaps(AsyncWebServerRequest* request) {
    _laps->clear();
    Serial.println("Laps cleared!");
    request->send(200, "application/json", "{\"status\":\"laps_cleared\"}");
}

void WebServerManager::handleSetFrequency(AsyncWebServerRequest* request) {
    if (request->hasParam("frequency")) {
        int freq = request->getParam("frequency")->value().toInt();
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
            // Use snprintf for efficient JSON building
            char response[128];
            snprintf(response, sizeof(response), "{\"status\":\"frequency_set\",\"frequency\":%d}", freq);
            request->send(200, "application/json", response);
        } else {
            request->send(400, "application/json", "{\"error\":\"invalid_frequency\"}");
        }
    } else {
        request->send(400, "application/json", "{\"error\":\"missing_frequency\"}");
    }
}

void WebServerManager::handleSetThreshold(AsyncWebServerRequest* request) {
    static char response[128];
    
    if (request->hasParam("enter_rssi") && request->hasParam("exit_rssi")) {
        int enter_rssi = request->getParam("enter_rssi")->value().toInt();
        int exit_rssi = request->getParam("exit_rssi")->value().toInt();
        if (enter_rssi >= 0 && enter_rssi <= 255 && exit_rssi >= 0 && exit_rssi <= 255 && enter_rssi > exit_rssi) {
            if (_timingCore) {
                _timingCore->setEnterRssi(enter_rssi);
                _timingCore->setExitRssi(exit_rssi);
                if (_settingsManager) {
                    _settingsManager->saveSettings(_timingCore);
                }
            }
            snprintf(response, sizeof(response), "{\"status\":\"threshold_set\",\"enter_rssi\":%d,\"exit_rssi\":%d}",
                    enter_rssi, exit_rssi);
            request->send(200, "application/json", response);
            Serial.printf("Thresholds set: Enter=%d, Exit=%d (saved)\n", enter_rssi, exit_rssi);
        } else {
            request->send(400, "application/json", "{\"error\":\"invalid_threshold\"}");
        }
    } else if (request->hasParam("threshold")) {
        int threshold = request->getParam("threshold")->value().toInt();
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
            snprintf(response, sizeof(response), "{\"status\":\"threshold_set\",\"threshold\":%d}", threshold);
            request->send(200, "application/json", response);
            Serial.printf("Threshold set to: %d (migrated to Enter=%d, Exit=%d, saved)\n", threshold, enter, exit);
        } else {
            request->send(400, "application/json", "{\"error\":\"invalid_threshold\"}");
        }
    } else {
        request->send(400, "application/json", "{\"error\":\"missing_threshold\"}");
    }
}

void WebServerManager::handleGetChannels(AsyncWebServerRequest* request) {
    // Static data - use pre-allocated buffer
    static const char* channelsJson = 
        "{\"bands\":{"
        "\"Raceband\":["
        "{\"channel\":\"R1\",\"frequency\":5658},{\"channel\":\"R2\",\"frequency\":5695},"
        "{\"channel\":\"R3\",\"frequency\":5732},{\"channel\":\"R4\",\"frequency\":5769},"
        "{\"channel\":\"R5\",\"frequency\":5806},{\"channel\":\"R6\",\"frequency\":5843},"
        "{\"channel\":\"R7\",\"frequency\":5880},{\"channel\":\"R8\",\"frequency\":5917}],"
        "\"Fatshark\":["
        "{\"channel\":\"F1\",\"frequency\":5740},{\"channel\":\"F2\",\"frequency\":5760},"
        "{\"channel\":\"F3\",\"frequency\":5780},{\"channel\":\"F4\",\"frequency\":5800},"
        "{\"channel\":\"F5\",\"frequency\":5820},{\"channel\":\"F6\",\"frequency\":5840},"
        "{\"channel\":\"F7\",\"frequency\":5860},{\"channel\":\"F8\",\"frequency\":5880}],"
        "\"Boscam_A\":["
        "{\"channel\":\"A1\",\"frequency\":5865},{\"channel\":\"A2\",\"frequency\":5845},"
        "{\"channel\":\"A3\",\"frequency\":5825},{\"channel\":\"A4\",\"frequency\":5805},"
        "{\"channel\":\"A5\",\"frequency\":5785},{\"channel\":\"A6\",\"frequency\":5765},"
        "{\"channel\":\"A7\",\"frequency\":5745},{\"channel\":\"A8\",\"frequency\":5725}],"
        "\"Boscam_E\":["
        "{\"channel\":\"E1\",\"frequency\":5705},{\"channel\":\"E2\",\"frequency\":5685},"
        "{\"channel\":\"E3\",\"frequency\":5665},{\"channel\":\"E4\",\"frequency\":5645},"
        "{\"channel\":\"E5\",\"frequency\":5885},{\"channel\":\"E6\",\"frequency\":5905},"
        "{\"channel\":\"E7\",\"frequency\":5925},{\"channel\":\"E8\",\"frequency\":5945}]"
        "}}";
    
    request->send(200, "application/json", channelsJson);
}

void WebServerManager::handleGetSPIFFSInfo(AsyncWebServerRequest* request) {
    static char jsonBuffer[JSON_SPIFFS_BUFFER_SIZE];
    
    bool mounted = SPIFFS.begin(false);
    int len = snprintf(jsonBuffer, JSON_SPIFFS_BUFFER_SIZE, "{\"mounted\":%s,", mounted ? "true" : "false");
    
    if (len < 0 || len >= (int)JSON_SPIFFS_BUFFER_SIZE) {
        request->send(500, "application/json", "{\"error\":\"buffer_overflow\"}");
        return;
    }

    if (mounted) {
        size_t totalBytes = SPIFFS.totalBytes();
        size_t usedBytes = SPIFFS.usedBytes();
        len += snprintf(jsonBuffer + len, JSON_SPIFFS_BUFFER_SIZE - len,
            "\"total_bytes\":%zu,\"used_bytes\":%zu,\"free_bytes\":%zu,\"files\":[",
            totalBytes, usedBytes, totalBytes - usedBytes);
        
        if (len < 0 || len >= (int)JSON_SPIFFS_BUFFER_SIZE) {
            request->send(500, "application/json", "{\"error\":\"buffer_overflow\"}");
            return;
        }

        File root = SPIFFS.open("/");
        if (root && root.isDirectory()) {
            File file = root.openNextFile();
            bool first = true;
            int fileCount = 0;
            const int MAX_FILES = 20;  // Limit files to prevent buffer overflow
            
            while (file && fileCount < MAX_FILES) {
                if (!first) {
                    len += snprintf(jsonBuffer + len, JSON_SPIFFS_BUFFER_SIZE - len, ",");
                    if (len < 0 || len >= (int)JSON_SPIFFS_BUFFER_SIZE) break;
                }
                
                // Escape filename and limit length to prevent overflow
                const char* name = file.name();
                size_t nameLen = strlen(name);
                if (nameLen > 64) nameLen = 64;  // Limit filename length
                
                len += snprintf(jsonBuffer + len, JSON_SPIFFS_BUFFER_SIZE - len,
                    "{\"name\":\"%.*s\",\"size\":%zu}", (int)nameLen, name, file.size());
                
                if (len < 0 || len >= (int)JSON_SPIFFS_BUFFER_SIZE) break;
                
                first = false;
                fileCount++;
                file = root.openNextFile();
            }
        }
        
        len += snprintf(jsonBuffer + len, JSON_SPIFFS_BUFFER_SIZE - len, "]");
    } else {
        len += snprintf(jsonBuffer + len, JSON_SPIFFS_BUFFER_SIZE - len, "\"error\":\"SPIFFS not mounted\"");
    }

    if (len > 0 && len < (int)JSON_SPIFFS_BUFFER_SIZE - 2) {
        len += snprintf(jsonBuffer + len, JSON_SPIFFS_BUFFER_SIZE - len, "}");
        if (len > 0 && len < (int)JSON_SPIFFS_BUFFER_SIZE) {
            request->send(200, "application/json", jsonBuffer);
            return;
        }
    }

    request->send(500, "application/json", "{\"error\":\"response_too_large\"}");
}

void WebServerManager::handleGetConfig(AsyncWebServerRequest* request) {
    Preferences prefs;

    if (!prefs.begin("sfos_pins", true)) {
        Serial.println("API: Failed to open NVS for reading pin config");
        request->send(404, "application/json", "{\"error\":\"Pin config not found in NVS\",\"exists\":false}");
        return;
    }

    uint8_t enabled = prefs.getUChar("pin_enabled", 0);
    if (enabled == 0) {
        prefs.end();
        Serial.println("API: Custom pin config not enabled in NVS");
        request->send(404, "application/json", "{\"error\":\"Custom pin config not enabled\",\"exists\":false}");
        return;
    }

    // Use static buffer for JSON serialization to prevent stack overflow
    static char jsonBuffer[JSON_CONFIG_BUFFER_SIZE];
    
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

    // Serialize to static buffer with size limit
    size_t bytesWritten = serializeJson(responseDoc, jsonBuffer, JSON_CONFIG_BUFFER_SIZE);
    
    if (bytesWritten == 0 || bytesWritten >= JSON_CONFIG_BUFFER_SIZE) {
        // Buffer overflow or serialization failed
        request->send(500, "application/json", "{\"error\":\"config_too_large\"}");
        return;
    }

    Serial.println("API: Serving pin config from NVS");
    request->send(200, "application/json", jsonBuffer);
}

void WebServerManager::handleStyleCSS(AsyncWebServerRequest* request) {
    // ESPAsyncWebServer handles SPIFFS file serving efficiently with automatic chunking
    // Temporarily pause timing core to give file transfer full CPU (prevents partial transfer errors)
    bool timingWasActive = false;
    
    if (SPIFFS.exists("/style.css")) {
        // Get file size to calculate pause duration (conservative estimate)
        File testFile = SPIFFS.open("/style.css", "r");
        size_t fileSize = testFile ? testFile.size() : 20000;  // Default to 20KB if can't read
        if (testFile) testFile.close();
        
        // Calculate pause time: file size / estimated WiFi speed (conservative: ~30KB/s for ESP32-C6)
        // Use 4x margin for safety: (fileSize / 30000) * 4000ms, minimum 4 seconds
        // ESPAsyncWebServer sends asynchronously, so we need generous pause time
        uint32_t pauseMs = ((fileSize * 4000) / 30000) + 4000;  // At least 4 seconds, more for larger files
        if (pauseMs > 15000) pauseMs = 15000;  // Cap at 15 seconds max
        
        // Pause timing core BEFORE sending file
        if (_timingCore) {
            timingWasActive = _timingCore->pauseTemporarily(pauseMs);
        }
        
        AsyncWebServerResponse* response = request->beginResponse(SPIFFS, "/style.css", "text/css");
        // Enable browser caching with 1 year expiration (versioned via ?v= in HTML)
        response->addHeader("Cache-Control", "public, max-age=31536000, immutable");
        response->addHeader("ETag", "\"style-v5\"");  // Version tag for cache validation
        request->send(response);
        
        // Schedule non-blocking resume after delay (prevents watchdog timeout)
        // ESPAsyncWebServer serves files asynchronously, so we schedule resume in background
        scheduleTimingResume(_timingCore, timingWasActive, pauseMs);
    } else {
        // Fallback error CSS
        request->send(200, "text/css",
            "body{font-family:Arial,sans-serif;background:#1a1f35;color:#fff;padding:40px;text-align:center;}"
            "h1{color:#ff7b00;margin-bottom:20px;}"
            ".error{background:#2a0f0f;border:2px solid #ff3838;border-radius:8px;padding:30px;max-width:600px;margin:0 auto;}"
        );
    }
}

void WebServerManager::handleAppJS(AsyncWebServerRequest* request) {
    // ESPAsyncWebServer handles SPIFFS file serving efficiently with automatic chunking
    // Temporarily pause timing core to give file transfer full CPU (prevents partial transfer errors)
    bool timingWasActive = false;
    
    if (SPIFFS.exists("/app.js")) {
        // Get file size to calculate pause duration (conservative estimate)
        File testFile = SPIFFS.open("/app.js", "r");
        size_t fileSize = testFile ? testFile.size() : 45000;  // Default to 45KB if can't read
        if (testFile) testFile.close();
        
        // Calculate pause time: file size / estimated WiFi speed (conservative: ~30KB/s for ESP32-C6)
        // Use 4x margin for safety: (fileSize / 30000) * 4000ms, minimum 6 seconds
        // ESPAsyncWebServer sends asynchronously, so we need generous pause time
        uint32_t pauseMs = ((fileSize * 4000) / 30000) + 6000;  // At least 6 seconds, more for larger files
        if (pauseMs > 20000) pauseMs = 20000;  // Cap at 20 seconds max
        
        // Pause timing core BEFORE sending file
        if (_timingCore) {
            timingWasActive = _timingCore->pauseTemporarily(pauseMs);
        }
        
        AsyncWebServerResponse* response = request->beginResponse(SPIFFS, "/app.js", "application/javascript");
        // Enable browser caching with 1 year expiration (versioned via ?v=5 in HTML)
        response->addHeader("Cache-Control", "public, max-age=31536000, immutable");
        response->addHeader("ETag", "\"app-v5\"");  // Version tag for cache validation
        request->send(response);
        
        // Schedule non-blocking resume after delay (prevents watchdog timeout)
        // ESPAsyncWebServer serves files asynchronously, so we schedule resume in background
        scheduleTimingResume(_timingCore, timingWasActive, pauseMs);
    } else {
        // Fallback error JavaScript
        request->send(200, "application/javascript",
            "console.error('app.js not found in SPIFFS - Please upload filesystem');"
            "document.body.innerHTML='<div class=\"error\"><h1>⚠️ Files Missing</h1>"
            "<p>Web interface files not found on device.</p>"
            "<p>Please run: <code>pio run -t uploadfs</code></p></div>';"
        );
    }
}

void WebServerManager::handleNotFound(AsyncWebServerRequest* request) {
    request->send(404, "text/plain", "File not found");
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
