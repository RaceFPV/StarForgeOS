#ifndef STANDALONE_MODE_H
#define STANDALONE_MODE_H

#include "platform/hal.h"
#include <vector>
#include "timing_core.h"
#include "config/config.h"

// Forward declarations
class SettingsManager;
class WiFiManager;
class WebServerManager;

#if ENABLE_BATTERY_MONITOR
class BatteryMonitor;
#endif

class StatusLed;

#if ENABLE_LCD_UI
#include "ui/lcd_ui.h"
class AudioOutput;
#endif

#if defined(BOARD_NUCLEARCOUNTER)
class BoardDisplays;
#endif

class StandaloneMode {
public:
    StandaloneMode();
    void begin(TimingCore* timingCore);
    void process();
    void saveSettings();  // Public so LCD UI can call it

private:
    TimingCore* _timingCore;
    std::vector<LapData> _laps;
    bool _raceActive = false;
    uint32_t _raceStartTime = 0;

    // Module managers (using composition)
    SettingsManager* _settingsManager;
    WiFiManager* _wifiManager;
    WebServerManager* _webServer;

#if ENABLE_BATTERY_MONITOR
    // Battery monitoring
    // May be nullptr if BATTERY_ADC_PIN is not defined
    BatteryMonitor* _batteryMonitor;
#endif

    // Status LED for activity indication (optional, disabled if STATUS_LED_PIN not defined)
    StatusLed* _statusLed;

#if ENABLE_LCD_UI
    // LCD UI
    LcdUI* _lcdUI;
    TaskHandle_t _lcdTaskHandle;

    // LCD button callbacks (called from LCD task, must be thread-safe)
    static void lcdStartCallback();
    static void lcdStopCallback();
    static void lcdClearCallback();
    static StandaloneMode* _lcdInstance;  // For static callbacks

#if ENABLE_AUDIO
    AudioOutput* _audioOutput;
#endif
#endif

#if defined(BOARD_NUCLEARCOUNTER)
    BoardDisplays* _boardDisplays;
#endif
};

#endif // STANDALONE_MODE_H
