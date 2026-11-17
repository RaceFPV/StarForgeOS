#include "web_server.h"
#include "platform/spiffs_vfs.h"
#include "mdns.h"
#include <dirent.h>
#include "config/config.h"
#include "config/config_globals.h"
#include "settings/settings_manager.h"
#include "platform/nvs_store.h"
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

WebServerManager::WebServerManager() : _server(nullptr), _timingCore(nullptr),
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

static WebServerManager* active_server = nullptr;

static esp_err_t route_root(httpd_req_t* req) { HttpRequest wrapped(req); active_server->handleRoot(&wrapped); return ESP_OK; }
static esp_err_t route_status(httpd_req_t* req) { HttpRequest wrapped(req); active_server->handleGetStatus(&wrapped); return ESP_OK; }
static esp_err_t route_laps(httpd_req_t* req) { HttpRequest wrapped(req); active_server->handleGetLaps(&wrapped); return ESP_OK; }
static esp_err_t route_rssi(httpd_req_t* req) { HttpRequest wrapped(req); active_server->handleGetRSSIHistory(&wrapped); return ESP_OK; }
static esp_err_t route_start(httpd_req_t* req) { HttpRequest wrapped(req); active_server->handleStartRace(&wrapped); return ESP_OK; }
static esp_err_t route_stop(httpd_req_t* req) { HttpRequest wrapped(req); active_server->handleStopRace(&wrapped); return ESP_OK; }
static esp_err_t route_clear(httpd_req_t* req) { HttpRequest wrapped(req); active_server->handleClearLaps(&wrapped); return ESP_OK; }
static esp_err_t route_frequency(httpd_req_t* req) { HttpRequest wrapped(req); active_server->handleSetFrequency(&wrapped); return ESP_OK; }
static esp_err_t route_threshold(httpd_req_t* req) { HttpRequest wrapped(req); active_server->handleSetThreshold(&wrapped); return ESP_OK; }
static esp_err_t route_channels(httpd_req_t* req) { HttpRequest wrapped(req); active_server->handleGetChannels(&wrapped); return ESP_OK; }
static esp_err_t route_spiffs(httpd_req_t* req) { HttpRequest wrapped(req); active_server->handleGetSPIFFSInfo(&wrapped); return ESP_OK; }
static esp_err_t route_config(httpd_req_t* req) { HttpRequest wrapped(req); active_server->handleGetConfig(&wrapped); return ESP_OK; }
static esp_err_t route_css(httpd_req_t* req) { HttpRequest wrapped(req); active_server->handleStyleCSS(&wrapped); return ESP_OK; }
static esp_err_t route_js(httpd_req_t* req) { HttpRequest wrapped(req); active_server->handleAppJS(&wrapped); return ESP_OK; }

static void register_route(httpd_handle_t server, const char* uri, httpd_method_t method, esp_err_t (*handler)(httpd_req_t*)) {
    httpd_uri_t route = {};
    route.uri = uri;
    route.method = method;
    route.handler = handler;
    httpd_register_uri_handler(server, &route);
}

void WebServerManager::begin(TimingCore* timingCore, SettingsManager* settingsManager,
                              bool* raceActive, uint32_t* raceStartTime, std::vector<LapData>* laps) {
    _timingCore = timingCore;
    _settingsManager = settingsManager;
    _raceActive = raceActive;
    _raceStartTime = raceStartTime;
    _laps = laps;
    active_server = this;

    if (!spiffs_mount(false) && !spiffs_mount(true)) {
        log_line("Warning: SPIFFS Mount Failed (index.html won't be available, but API will work)");
    } else {
        log_fmt("SPIFFS Partition: %u bytes total, %u bytes used\n",
                (unsigned)spiffs_total_bytes(), (unsigned)spiffs_used_bytes());
    }

    mdns_init();
    mdns_hostname_set(MDNS_HOSTNAME);
    mdns_instance_name_set("StarForge");
    mdns_service_add(nullptr, "_http", "_tcp", WEB_SERVER_PORT, nullptr, 0);
    log_fmt("mDNS responder started: %s.local\n", MDNS_HOSTNAME);

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = WEB_SERVER_PORT;
    config.max_uri_handlers = 20;
    config.stack_size = 8192;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 10;
    config.send_wait_timeout = 30;
    if (httpd_start(&_server, &config) != ESP_OK) {
        log_line("ERROR: HTTP server failed to start");
        return;
    }

    register_route(_server, "/", HTTP_GET, route_root);
    register_route(_server, "/test", HTTP_GET, route_root);
    register_route(_server, "/api/status", HTTP_GET, route_status);
    register_route(_server, "/api/laps", HTTP_GET, route_laps);
    register_route(_server, "/api/rssi_history", HTTP_GET, route_rssi);
    register_route(_server, "/api/start_race", HTTP_POST, route_start);
    register_route(_server, "/api/stop_race", HTTP_POST, route_stop);
    register_route(_server, "/api/clear_laps", HTTP_POST, route_clear);
    register_route(_server, "/api/set_frequency", HTTP_POST, route_frequency);
    register_route(_server, "/api/set_threshold", HTTP_POST, route_threshold);
    register_route(_server, "/api/get_channels", HTTP_GET, route_channels);
    register_route(_server, "/api/spiffs_info", HTTP_GET, route_spiffs);
    register_route(_server, "/api/config", HTTP_GET, route_config);
    register_route(_server, "/style.css", HTTP_GET, route_css);
    register_route(_server, "/app.js", HTTP_GET, route_js);

    log_line("Web server started");
    log_line("IP address: 192.168.4.1");
    log_fmt("mDNS hostname: %s.local\n", MDNS_HOSTNAME);
    log_line("Open browser to http://192.168.4.1 or http://sfos.local");
}

void WebServerManager::sendStoredFile(HttpRequest* request, const char* web_path, const char* content_type,
                                      const char* cache_control, uint32_t pause_ms) {
    if (!spiffs_exists(web_path)) {
        request->send(404, "text/plain", "File not found");
        return;
    }
    bool timing_was_active = false;
    if (_timingCore && pause_ms > 0) {
        timing_was_active = _timingCore->pauseTemporarily(pause_ms);
    }
    char path[128];
    spiffs_vfs_path(web_path, path, sizeof(path));
    FILE* file = fopen(path, "rb");
    if (!file) {
        request->send(404, "text/plain", "File not found");
        return;
    }
    httpd_resp_set_type(request->raw(), content_type);
    if (cache_control) {
        httpd_resp_set_hdr(request->raw(), "Cache-Control", cache_control);
    }
    char buffer[1024];
    size_t read_count = 0;
    while ((read_count = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        if (httpd_resp_send_chunk(request->raw(), buffer, read_count) != ESP_OK) {
            break;
        }
    }
    fclose(file);
    httpd_resp_send_chunk(request->raw(), nullptr, 0);
    if (pause_ms > 0) {
        scheduleTimingResume(_timingCore, timing_was_active, pause_ms);
    }
}

void WebServerManager::handleRoot(HttpRequest* request) {
    if (!spiffs_exists("/index.html")) {
        request->send(404, "text/plain", "index.html not found");
        return;
    }
    sendStoredFile(request, "/index.html", "text/html", "public, max-age=300, must-revalidate", 0);
}

void WebServerManager::handleGetStatus(HttpRequest* request) {
    // Pre-allocated buffer for status JSON (most frequently called handler)
    static char jsonBuffer[JSON_STATUS_BUFFER_SIZE];
    
    uint8_t current_rssi = _timingCore ? _timingCore->getCurrentRSSI() : 0;
    uint16_t frequency = _timingCore ? _timingCore->getState().frequency_mhz : 5800;
    uint8_t enter_rssi = _timingCore ? _timingCore->getEnterRssi() : 120;
    uint8_t exit_rssi = _timingCore ? _timingCore->getExitRssi() : 100;
    bool crossing = _timingCore ? _timingCore->isCrossing() : false;
    size_t lapCount = _laps ? _laps->size() : 0;
    uint32_t uptime = hal_millis();

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

void WebServerManager::handleGetLaps(HttpRequest* request) {
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

void WebServerManager::handleGetRSSIHistory(HttpRequest* request) {
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

void WebServerManager::handleStartRace(HttpRequest* request) {
    *_raceActive = true;
    *_raceStartTime = hal_millis();
    _laps->clear();

    if (_timingCore) {
        while (_timingCore->hasNewLap()) {
            _timingCore->getNextLap();
        }
    }

    log_line("Race started!");
    request->send(200, "application/json", "{\"status\":\"race_started\"}");
}

void WebServerManager::handleStopRace(HttpRequest* request) {
    *_raceActive = false;
    log_line("Race stopped!");
    request->send(200, "application/json", "{\"status\":\"race_stopped\"}");
}

void WebServerManager::handleClearLaps(HttpRequest* request) {
    _laps->clear();
    log_line("Laps cleared!");
    request->send(200, "application/json", "{\"status\":\"laps_cleared\"}");
}

void WebServerManager::handleSetFrequency(HttpRequest* request) {
    if (request->hasParam("frequency", true)) {
        int freq = request->getParam("frequency", true)->value().toInt();
        if (freq >= 5645 && freq <= 5945) {
            if (_timingCore) {
                uint8_t band, channel;
                findBandChannelFromFrequency(freq, band, channel);
                _timingCore->setRX5808Settings(band, channel);
                if (_settingsManager) {
                    _settingsManager->saveSettings(_timingCore);
                }
                log_fmt("Frequency set to: %d MHz (Band=%d, Channel=%d, saved)\n",
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

void WebServerManager::handleSetThreshold(HttpRequest* request) {
    static char response[128];
    
    if (request->hasParam("enter_rssi", true) && request->hasParam("exit_rssi", true)) {
        int enter_rssi = request->getParam("enter_rssi", true)->value().toInt();
        int exit_rssi = request->getParam("exit_rssi", true)->value().toInt();
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
            log_fmt("Thresholds set: Enter=%d, Exit=%d (saved)\n", enter_rssi, exit_rssi);
        } else {
            request->send(400, "application/json", "{\"error\":\"invalid_threshold\"}");
        }
    } else if (request->hasParam("threshold", true)) {
        int threshold = request->getParam("threshold", true)->value().toInt();
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
            log_fmt("Threshold set to: %d (migrated to Enter=%d, Exit=%d, saved)\n", threshold, enter, exit);
        } else {
            request->send(400, "application/json", "{\"error\":\"invalid_threshold\"}");
        }
    } else {
        request->send(400, "application/json", "{\"error\":\"missing_threshold\"}");
    }
}

void WebServerManager::handleGetChannels(HttpRequest* request) {
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

void WebServerManager::handleGetSPIFFSInfo(HttpRequest* request) {
    static char jsonBuffer[JSON_SPIFFS_BUFFER_SIZE];
    bool mounted = spiffs_ready();
    int len = snprintf(jsonBuffer, JSON_SPIFFS_BUFFER_SIZE,
                       "{\"mounted\":%s,\"total\":%u,\"used\":%u,\"files\":[",
                       mounted ? "true" : "false",
                       (unsigned)spiffs_total_bytes(), (unsigned)spiffs_used_bytes());
    DIR* dir = mounted ? opendir("/spiffs") : nullptr;
    bool first = true;
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr && len < (int)JSON_SPIFFS_BUFFER_SIZE - 80) {
            if (entry->d_name[0] == '.') continue;
            char web_path[64];
            snprintf(web_path, sizeof(web_path), "/%s", entry->d_name);
            len += snprintf(jsonBuffer + len, JSON_SPIFFS_BUFFER_SIZE - len,
                            "%s{\"name\":\"%s\",\"size\":%u}",
                            first ? "" : ",", entry->d_name, (unsigned)spiffs_file_size(web_path));
            first = false;
        }
        closedir(dir);
    }
    snprintf(jsonBuffer + len, JSON_SPIFFS_BUFFER_SIZE - len, "]}");
    request->send(200, "application/json", jsonBuffer);
}

void WebServerManager::handleGetConfig(HttpRequest* request) {
    NvsStore prefs;
    if (!prefs.open("sfos_pins", true)) {
        request->send(404, "application/json", "{\"error\":\"Pin config not found in NVS\",\"exists\":false}");
        return;
    }
    uint8_t enabled = prefs.getUChar("pin_enabled", 0);
    if (enabled == 0) {
        prefs.close();
        request->send(404, "application/json", "{\"error\":\"Custom pin config not enabled\",\"exists\":false}");
        return;
    }
    static char jsonBuffer[JSON_CONFIG_BUFFER_SIZE];
    int len = snprintf(jsonBuffer, JSON_CONFIG_BUFFER_SIZE,
        "{\"exists\":true,\"source\":\"NVS\",\"content\":{\"custom_pins\":{"
        "\"enabled\":true,\"rssi_input\":%u,\"rx5808_data\":%u,\"rx5808_clk\":%u,\"rx5808_sel\":%u",
        prefs.getUChar("pin_rssi_input", 0), prefs.getUChar("pin_rx5808_data", 0),
        prefs.getUChar("pin_rx5808_clk", 0), prefs.getUChar("pin_rx5808_sel", 0));
    if (prefs.isKey("pin_power_button")) {
        len += snprintf(jsonBuffer + len, JSON_CONFIG_BUFFER_SIZE - len, ",\"power_button\":%u", prefs.getUChar("pin_power_button", 0));
    }
    if (prefs.isKey("pin_battery_adc")) {
        len += snprintf(jsonBuffer + len, JSON_CONFIG_BUFFER_SIZE - len, ",\"battery_adc\":%u", prefs.getUChar("pin_battery_adc", 0));
    }
    if (prefs.isKey("pin_audio_dac")) {
        len += snprintf(jsonBuffer + len, JSON_CONFIG_BUFFER_SIZE - len, ",\"audio_dac\":%u", prefs.getUChar("pin_audio_dac", 0));
    }
    if (prefs.isKey("pin_usb_detect")) {
        len += snprintf(jsonBuffer + len, JSON_CONFIG_BUFFER_SIZE - len, ",\"usb_detect\":%u", prefs.getUChar("pin_usb_detect", 0));
    }
    if (prefs.isKey("pin_lcd_i2c_sda")) {
        len += snprintf(jsonBuffer + len, JSON_CONFIG_BUFFER_SIZE - len, ",\"lcd_i2c_sda\":%d", prefs.getChar("pin_lcd_i2c_sda", -1));
    }
    if (prefs.isKey("pin_lcd_i2c_scl")) {
        len += snprintf(jsonBuffer + len, JSON_CONFIG_BUFFER_SIZE - len, ",\"lcd_i2c_scl\":%d", prefs.getChar("pin_lcd_i2c_scl", -1));
    }
    if (prefs.isKey("pin_lcd_backlight")) {
        len += snprintf(jsonBuffer + len, JSON_CONFIG_BUFFER_SIZE - len, ",\"lcd_backlight\":%d", prefs.getChar("pin_lcd_backlight", -1));
    }
    prefs.close();
    snprintf(jsonBuffer + len, JSON_CONFIG_BUFFER_SIZE - len, "}}}");
    request->send(200, "application/json", jsonBuffer);
}

void WebServerManager::handleStyleCSS(HttpRequest* request) {
    if (!spiffs_exists("/style.css")) {
        request->send(200, "text/css", "body{font-family:Arial,sans-serif;background:#1a1f35;color:#fff;}");
        return;
    }
    uint32_t pause_ms = (uint32_t)((spiffs_file_size("/style.css") * 4000) / 30000) + 4000;
    if (pause_ms > 15000) pause_ms = 15000;
    sendStoredFile(request, "/style.css", "text/css", "public, max-age=31536000, immutable", pause_ms);
}

void WebServerManager::handleAppJS(HttpRequest* request) {
    if (!spiffs_exists("/app.js")) {
        request->send(200, "application/javascript",
            "console.error('app.js not found');");
        return;
    }
    uint32_t pause_ms = (uint32_t)((spiffs_file_size("/app.js") * 4000) / 30000) + 6000;
    if (pause_ms > 20000) pause_ms = 20000;
    sendStoredFile(request, "/app.js", "application/javascript", "public, max-age=31536000, immutable", pause_ms);
}

void WebServerManager::handleNotFound(HttpRequest* request) {
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
