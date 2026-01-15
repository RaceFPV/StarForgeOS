#include "standalone_mode.h"
#include "config/config_globals.h"
#include "settings/settings_manager.h"
#include "settings/wifi_manager.h"
#include "web/web_server.h"

#if ENABLE_BATTERY_MONITOR && defined(BATTERY_ADC_PIN)
#include "hardware/battery_monitor.h"
#endif

#if defined(STATUS_LED_PIN)
#include "hardware/status_led.h"
#endif

#if ENABLE_LCD_UI
#include "hardware/audio_output.h"
#endif

#if defined(BOARD_NUCLEARCOUNTER)
#include "hardware/board_displays.h"
#endif

#if ENABLE_LCD_UI
StandaloneMode* StandaloneMode::_lcdInstance = nullptr;
#endif

StandaloneMode::StandaloneMode() : _timingCore(nullptr) {
    // Create module managers
    _settingsManager = new SettingsManager();
    _wifiManager = new WiFiManager();
    _webServer = new WebServerManager();

#if ENABLE_BATTERY_MONITOR
    // Battery monitoring only works in standalone mode (not in RotorHazard node mode)
    #if defined(BATTERY_ADC_PIN)
        // Initialize battery monitoring (available on boards with or without LCD)
        _batteryMonitor = new BatteryMonitor();
    #else
        // Battery monitoring enabled but pin not defined - will warn in begin()
        _batteryMonitor = nullptr;
    #endif
#else
    // Battery monitoring disabled at compile time
    #if defined(BATTERY_ADC_PIN)
        // Pin defined but monitoring disabled - this is fine, just ignore it
    #endif
#endif

    // Initialize status LED (optional, disabled if STATUS_LED_PIN not defined)
#if defined(STATUS_LED_PIN)
    _statusLed = new StatusLed();
#else
    _statusLed = nullptr;
#endif

#if ENABLE_LCD_UI
    _lcdUI = nullptr;
    _lcdInstance = this;

#if ENABLE_AUDIO
    _audioOutput = new AudioOutput();
#endif
#endif

#if defined(BOARD_NUCLEARCOUNTER)
    _boardDisplays = new BoardDisplays();
#endif
}

void StandaloneMode::begin(TimingCore* timingCore) {
    _timingCore = timingCore;

#if ENABLE_BATTERY_MONITOR
    // Battery monitoring only works in standalone mode (not in RotorHazard node mode)
    #if defined(BATTERY_ADC_PIN)
        // Initialize battery monitoring (available on boards with or without LCD)
        _batteryMonitor->begin();
    #else
        // Battery monitoring enabled but pin not defined - silently fail with warning
        Serial.println("Warning: ENABLE_BATTERY_MONITOR=1 but BATTERY_ADC_PIN not defined. Battery monitoring disabled.");
    #endif
#endif

    // Initialize status LED - rapid blink in standalone mode (3 times per second)
#if defined(STATUS_LED_PIN)
    if (_statusLed) {
        _statusLed->init(STATUS_LED_PIN, STATUS_LED_INVERTED);
#if defined(STATUS_LED_WS2812) && STATUS_LED_WS2812
        // Set blue color for standalone mode (WS2812 RGB LED) - dim brightness
        _statusLed->setColor(0, 0, 20);  // Blue (dimmed)
#endif
        // Rapid blink: 3 times per second = ~150ms on, 183ms off
        _statusLed->blink(150, 183);
        Serial.printf("Status LED initialized on GPIO%d (standalone mode: rapid blink)\n", STATUS_LED_PIN);
    }
#endif

#if ENABLE_LCD_UI && ENABLE_AUDIO
    // Initialize audio DAC
    _audioOutput->begin();
#endif

    // Initialize WiFi AP with stability improvements
    _wifiManager->setupAP();

    // Give WiFi time to stabilize
    delay(600);

    // Allocate RSSI history buffer NOW (after WiFi init, before web server)
    // This gives us the best chance of getting a contiguous block before heap fragmentation
#if RSSI_HISTORY_ENABLED
    _timingCore->allocateRSSIHistory();
#endif

    // Initialize web server
    _webServer->begin(_timingCore, _settingsManager, &_raceActive, &_raceStartTime, &_laps);

#if defined(BOARD_NUCLEARCOUNTER)
    // Initialize NuclearCounter display
    _boardDisplays->initNuclearCounter(_wifiManager->getSSID());
#endif

    // Load saved settings from flash BEFORE creating LCD UI
    // This ensures the correct values are displayed on boot
    _settingsManager->loadSettings(_timingCore);

    // Note: ESPAsyncWebServer doesn't need a separate task - it's async and handles requests automatically

#if ENABLE_LCD_UI
    // IMPORTANT: Give WiFi more time to fully stabilize before initializing LCD
    // LCD initialization uses I2C which can cause issues if WiFi isn't fully ready
    delay(500);

    // Initialize LCD UI (optional feature)
    Serial.println("\n====================================");
    Serial.println("Initializing LCD UI (optional)");
    Serial.println("====================================");

    _lcdUI = new LcdUI();
    if (_lcdUI && _lcdUI->begin()) {
        // Set button callbacks
        _lcdUI->setStartCallback(lcdStartCallback);
        _lcdUI->setStopCallback(lcdStopCallback);
        _lcdUI->setClearCallback(lcdClearCallback);
        _lcdUI->setSettingsChangedCallback([]() {
            // Called when user changes settings via LCD
            if (_lcdInstance) {
                _lcdInstance->saveSettings();
            }
        });
        _lcdUI->setTimingCore(_timingCore);  // Link timing core for settings access

        // Initialize settings display
        uint8_t band, channel;
        _timingCore->getRX5808Settings(band, channel);
        _lcdUI->updateBandChannel(band, channel);
        _lcdUI->updateFrequency(_timingCore->getCurrentFrequency());

        // Get and display current thresholds from timing core
        uint8_t enter_rssi = _timingCore->getEnterRssi();
        uint8_t exit_rssi = _timingCore->getExitRssi();
        Serial.printf("LCD: Displaying thresholds: Enter=%d, Exit=%d\n", enter_rssi, exit_rssi);
        _lcdUI->updateThreshold(enter_rssi);  // Update with enter threshold for now

        // Create LCD UI task
        // ESP32 dual-core: Pin to Core 0 (with WiFi/web, low priority)
        // ESP32-C3/C6 (single core): No core pinning
        // Stack increased to 8KB for LVGL with full-screen rendering
#if defined(CONFIG_IDF_TARGET_ESP32C6) || defined(ARDUINO_ESP32C3_DEV) || defined(CONFIG_IDF_TARGET_ESP32C3)
        xTaskCreate(LcdUI::uiTask, "LcdUI", 8192, _lcdUI, LCD_PRIORITY, &_lcdTaskHandle);
        Serial.println("LCD UI task created");
#else
        xTaskCreatePinnedToCore(LcdUI::uiTask, "LcdUI", 8192, _lcdUI, LCD_PRIORITY, &_lcdTaskHandle, 0);
        Serial.println("LCD UI task created on Core 0");
#endif
    } else {
        Serial.println("Warning: LCD UI initialization failed (optional feature)");
        if (_lcdUI) {
            delete _lcdUI;
            _lcdUI = nullptr;
        }
    }
#endif

    Serial.println("Setup complete!");
}

void StandaloneMode::process() {
    // mDNS handles requests automatically in background

    // Check for new lap data - only record during active race
    if (_raceActive && _timingCore && _timingCore->hasNewLap()) {
        LapData lap = _timingCore->getNextLap();

        // Grace period: Don't count laps in first 3 seconds after race start
        uint32_t time_since_start = millis() - _raceStartTime;
        if (time_since_start < 3000) {
            Serial.printf("Lap ignored (grace period): %dms after start\n", time_since_start);
            return;  // Skip this lap
        }

        // Store lap in our internal list (simple vector)
        _laps.push_back(lap);

        // Limit stored laps to prevent memory issues
        if (_laps.size() > 100) {
            _laps.erase(_laps.begin());
        }

        Serial.printf("Lap recorded: %dms, RSSI: %d\n", lap.timestamp_ms, lap.rssi_peak);

#if ENABLE_LCD_UI
        // Update LCD lap count and lap times
        if (_lcdUI) {
            _lcdUI->updateLapCount(_laps.size());
            _lcdUI->updateLapTimes(_laps);
        }

#if ENABLE_AUDIO
        // Calculate lap time (difference from previous lap or start)
        uint32_t lapTime = 0;
        if (_laps.size() == 1) {
            lapTime = lap.timestamp_ms - _raceStartTime;
        } else {
            lapTime = lap.timestamp_ms - _laps[_laps.size() - 2].timestamp_ms;
        }
        // Speak lap announcement with time
        _audioOutput->speakLapAnnouncement(_laps.size(), lapTime);
#endif
#endif
    }

#if ENABLE_BATTERY_MONITOR
    // Battery monitoring polling (works for both LCD and web GUI)
    // Note: Battery monitoring only runs in standalone mode, never in RotorHazard node mode
    // Update battery status every 5 seconds and share with both display systems
    if (_batteryMonitor != nullptr) {
        static unsigned long last_battery_update = 0;
        if (millis() - last_battery_update > 5000) {
            float voltage = _batteryMonitor->readVoltage();
            uint8_t percentage = _batteryMonitor->calculatePercentage(voltage);

#if defined(USB_DETECT_PIN)
            bool isCharging = _batteryMonitor->isUSBConnected();
#else
            bool isCharging = false;
#endif

            // Update web server cache (always - works for boards with or without LCD)
            _webServer->updateBatteryStatus(voltage, percentage, isCharging);

#if ENABLE_LCD_UI
            // Update LCD display (if LCD UI is enabled)
            if (_lcdUI) {
                _lcdUI->updateBattery(voltage, percentage, isCharging);
            }
#endif

            last_battery_update = millis();
        }
    }
#endif

#if ENABLE_LCD_UI
    // Update LCD RSSI (every loop iteration for real-time display)
    if (_lcdUI && _timingCore) {
        _lcdUI->updateRSSI(_timingCore->getCurrentRSSI());

        // Update settings display periodically (every 100ms is enough)
        static unsigned long last_settings_update = 0;
        if (millis() - last_settings_update > 100) {
            uint8_t band, channel;
            _timingCore->getRX5808Settings(band, channel);
            _lcdUI->updateBandChannel(band, channel);
            _lcdUI->updateFrequency(_timingCore->getCurrentFrequency());

            // Update threshold display (should always match config)
            uint8_t enter_rssi = _timingCore->getEnterRssi();
            _lcdUI->updateThreshold(enter_rssi);  // Update with enter threshold for now

            last_settings_update = millis();
        }
    }
#endif

    // Update status LED (throttled to every 10ms to avoid blocking main loop)
#if defined(STATUS_LED_PIN)
    if (_statusLed) {
        static uint32_t last_led_update = 0;
        uint32_t current_time = millis();
        // Only update LED every 10ms to prevent blocking and reduce overhead
        if (current_time - last_led_update >= 10) {
            _statusLed->update(current_time);
            last_led_update = current_time;
        }
    }
#endif
}

void StandaloneMode::saveSettings() {
    _settingsManager->saveSettings(_timingCore);
}

#if ENABLE_LCD_UI
// LCD button callbacks (static, called from LCD UI task)
void StandaloneMode::lcdStartCallback() {
    if (_lcdInstance) {
        _lcdInstance->_raceActive = true;
        _lcdInstance->_raceStartTime = millis();
        _lcdInstance->_laps.clear();

        // Drain any pending laps from timing core that may have been detected before race start
        // This ensures the first lap after race start is counted as lap 1
        if (_lcdInstance->_timingCore) {
            while (_lcdInstance->_timingCore->hasNewLap()) {
                _lcdInstance->_timingCore->getNextLap();  // Discard stale laps
            }
        }

        if (_lcdInstance->_lcdUI) {
            _lcdInstance->_lcdUI->updateRaceStatus(true);
            _lcdInstance->_lcdUI->updateLapCount(0);
            _lcdInstance->_lcdUI->updateLapTimes(_lcdInstance->_laps);  // Clear lap times display
        }

        Serial.println("[LCD] Race started!");
    }
}

void StandaloneMode::lcdStopCallback() {
    if (_lcdInstance) {
        _lcdInstance->_raceActive = false;

        if (_lcdInstance->_lcdUI) {
            _lcdInstance->_lcdUI->updateRaceStatus(false);
        }

        Serial.println("[LCD] Race stopped!");
    }
}

void StandaloneMode::lcdClearCallback() {
    if (_lcdInstance) {
        _lcdInstance->_laps.clear();

        if (_lcdInstance->_lcdUI) {
            _lcdInstance->_lcdUI->updateLapCount(0);
            _lcdInstance->_lcdUI->updateLapTimes(_lcdInstance->_laps);  // Clear lap times display
        }

        Serial.println("[LCD] Laps cleared!");
    }
}
#endif
