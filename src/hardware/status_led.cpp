#include "status_led.h"
#include "config/config.h"

#if defined(STATUS_LED_WS2812) && STATUS_LED_WS2812
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"

static rmt_channel_handle_t ws_channel = nullptr;
static rmt_encoder_handle_t ws_encoder = nullptr;

static void ws2812_write(int pin, uint8_t r, uint8_t g, uint8_t b) {
    if (!ws_channel) {
        rmt_tx_channel_config_t channel = {};
        channel.gpio_num = (gpio_num_t)pin;
        channel.clk_src = RMT_CLK_SRC_DEFAULT;
        channel.resolution_hz = 10000000;
        channel.mem_block_symbols = 64;
        channel.trans_queue_depth = 1;
        rmt_new_tx_channel(&channel, &ws_channel);
        rmt_bytes_encoder_config_t encoder = {};
        encoder.bit0.level0 = 1;
        encoder.bit0.duration0 = 3;
        encoder.bit0.level1 = 0;
        encoder.bit0.duration1 = 9;
        encoder.bit1.level0 = 1;
        encoder.bit1.duration0 = 9;
        encoder.bit1.level1 = 0;
        encoder.bit1.duration1 = 3;
        encoder.flags.msb_first = 1;
        rmt_new_bytes_encoder(&encoder, &ws_encoder);
        rmt_enable(ws_channel);
    }
    uint8_t grb[3] = {g, r, b};
    rmt_transmit_config_t tx = {};
    rmt_transmit(ws_channel, ws_encoder, grb, sizeof(grb), &tx);
    rmt_tx_wait_all_done(ws_channel, 20);
}
#endif

// Helper function to set LED state (GPIO or WS2812)
void StatusLed::_setLedState(uint8_t state) {
#if defined(STATUS_LED_WS2812) && STATUS_LED_WS2812
    // WS2812 mode: use rgbLedWrite() (replaces deprecated neopixelWrite)
    // HIGH = on (use stored color), LOW = off (0,0,0)
    if (state == 1) {
        ws2812_write(_ledPin, _ws2812_r, _ws2812_g, _ws2812_b);
    } else {
        ws2812_write(_ledPin, 0, 0, 0);
    }
#else
    // GPIO mode: use digitalWrite
    hal_gpio_write(_ledPin, state);
#endif
}

void StatusLed::init(uint8_t pin, bool inverted) {
    
    _ledPin = pin;
    _inverted = inverted;
    _initialState = inverted ? 1 : 0;
    _currentState = _initialState;
    
#if defined(STATUS_LED_WS2812) && STATUS_LED_WS2812
    // WS2812 mode: Initialize with rgbLedWrite() (replaces deprecated neopixelWrite)
    // Pin is 10 for ESP32-C3-Zero, just call rgbLedWrite(pin, r, g, b)
    // Do NOT call pinMode() - rgbLedWrite() handles everything
    ws2812_write(_ledPin, 0, 0, 0);
#else
    // GPIO mode: standard LED
    hal_gpio_output(_ledPin);
    hal_gpio_write(_ledPin, _currentState);
#endif
    
    _state = STATUS_LED_IDLE;
}

void StatusLed::update(uint32_t currentTimeMs) {
    // Early return for idle state - no work needed
    if (_state == STATUS_LED_IDLE) {
        return;
    }
    
    // Time wrap-around protection (check once at start)
    if (currentTimeMs < _lastUpdateMs) {
        _lastUpdateMs = currentTimeMs;
        return;
    }
    
    switch (_state) {
        case STATUS_LED_BLINKING:
            // Continuous blink pattern - only update when state needs to change
            if (_currentState == !_initialState) {
                // Currently ON - check if time to turn OFF
                if ((currentTimeMs - _lastUpdateMs) >= _onTimeMs) {
                    _currentState = _initialState;
                    _setLedState(_currentState);
                    _lastUpdateMs = currentTimeMs;
                }
            } else {
                // Currently OFF - check if time to turn ON
                if ((currentTimeMs - _lastUpdateMs) >= _offTimeMs) {
                    _currentState = !_initialState;
                    _setLedState(_currentState);
                    _lastUpdateMs = currentTimeMs;
                }
            }
            break;
            
        case STATUS_LED_ON:
            // LED on for specific duration
            if (_onTimeMs > 0 && (currentTimeMs - _lastUpdateMs) >= _onTimeMs) {
                // Time to turn off
                _currentState = _initialState;
                _setLedState(_currentState);
                _state = STATUS_LED_IDLE;
            }
            break;
            
        case STATUS_LED_ACTIVITY:
            // Quick flash on activity - automatically returns to idle
            if ((currentTimeMs - _lastUpdateMs) >= ACTIVITY_FLASH_MS) {
                // Flash duration elapsed - turn off
                _currentState = _initialState;
                _setLedState(_currentState);
                _state = STATUS_LED_IDLE;
            }
            break;
            
        default:
            break;
    }
}

void StatusLed::on(uint32_t timeMs) {
    
    if (timeMs > 0) {
        _state = STATUS_LED_ON;
        _onTimeMs = timeMs;
    } else {
        _state = STATUS_LED_IDLE;
    }
    
    _currentState = !_initialState;
    _setLedState(_currentState);
    _lastUpdateMs = hal_millis();
}

void StatusLed::off() {
    
    _state = STATUS_LED_IDLE;
    _currentState = _initialState;
    _setLedState(_currentState);
}

void StatusLed::blink(uint32_t onTimeMs, uint32_t offTimeMs) {
    
    _onTimeMs = onTimeMs;
    if (offTimeMs > 0) {
        _offTimeMs = offTimeMs;
    } else {
        _offTimeMs = onTimeMs;  // Default: equal on/off time
    }
    
    _state = STATUS_LED_BLINKING;
    _currentState = !_initialState;  // Start with LED ON
    _setLedState(_currentState);
    _lastUpdateMs = hal_millis();
}

void StatusLed::activity() {
    
    _state = STATUS_LED_ACTIVITY;
    _currentState = !_initialState;  // Turn LED ON
    _setLedState(_currentState);
    _lastUpdateMs = hal_millis();
}

void StatusLed::setColor(uint8_t r, uint8_t g, uint8_t b) {
#if defined(STATUS_LED_WS2812) && STATUS_LED_WS2812
    
    // Store color and update immediately (like Waveshare example)
    _ws2812_r = r;
    _ws2812_g = g;
    _ws2812_b = b;
    
    // Update LED if it's currently on
    if (_currentState == !_initialState) {
        ws2812_write(_ledPin, _ws2812_r, _ws2812_g, _ws2812_b);
    }
#else
    // GPIO LED - color setting not supported, just ignore
    (void)r; (void)g; (void)b;
#endif
}
