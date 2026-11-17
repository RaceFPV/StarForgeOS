#ifndef STATUS_LED_H
#define STATUS_LED_H

#include "platform/hal.h"
#include "config/config.h"

// rgbLedWrite() and neopixelWrite() are built-in ESP32 Arduino Core functions

// Status LED states
typedef enum {
    STATUS_LED_IDLE,      // LED off (no activity)
    STATUS_LED_BLINKING,  // Continuous blink pattern
    STATUS_LED_ON,        // LED on for a specific duration
    STATUS_LED_ACTIVITY   // Quick flash on activity (web/RSSI)
} status_led_state_e;

class StatusLed {
public:
    // Initialize the LED (pin -1 or undefined disables LED)
    void init(uint8_t pin, bool inverted = false);
    
    // Update LED state (call regularly in main loop)
    void update(uint32_t currentTimeMs);
    
    // Control methods
    void on(uint32_t timeMs = 0);           // Turn LED on (optionally for duration)
    void off();                              // Turn LED off
    void blink(uint32_t onTimeMs, uint32_t offTimeMs = 0);  // Continuous blink
    void activity();                         // Quick flash for activity (web/RSSI)
    
    // Set color (only works for WS2812 LEDs, ignored for GPIO LEDs)
    void setColor(uint8_t r, uint8_t g, uint8_t b);

private:
    status_led_state_e _state = STATUS_LED_IDLE;
    uint8_t _ledPin = 255;           // Pin number (255 = disabled)
    bool _inverted = false;
    uint8_t _initialState = 0;
    uint8_t _currentState = 0;
    
    uint32_t _onTimeMs = 0;
    uint32_t _offTimeMs = 0;
    uint32_t _lastUpdateMs = 0;
    
    // Activity flash timing
    static const uint32_t ACTIVITY_FLASH_MS = 50;  // Quick 50ms flash
    
    // Helper function to set LED state (handles both GPIO and WS2812)
    void _setLedState(uint8_t state);
    
#if defined(STATUS_LED_WS2812) && STATUS_LED_WS2812
    // WS2812 RGB LED support - just store the current color (dimmed for comfort)
    uint8_t _ws2812_r = 0;           // Red
    uint8_t _ws2812_g = 0;           // Green  
    uint8_t _ws2812_b = 0;           // Blue
#endif
};

#endif // STATUS_LED_H

