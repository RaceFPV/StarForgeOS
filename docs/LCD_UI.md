# LCD Touchscreen UI (Optional Feature)

## Overview
The StarForge OS includes **optional** LCD touchscreen support for the JC2432W328C board (ESP32-D0WD-V3 with 240x320 ILI9341/ST7789 display and CST820 capacitive touch).

When enabled, it provides a local UI with:
- Real-time RSSI display (large, easy to read)
- Lap counter
- START/STOP/CLEAR buttons for race control
- Touch-responsive interface using LVGL

## Enabling LCD UI

### 1. Edit `src/config.h`
Change this line:
```cpp
#define ENABLE_LCD_UI       0     // Disabled by default
```

To:
```cpp
#define ENABLE_LCD_UI       1     // Enable LCD UI
```

### 2. Update `platformio.ini`
Add LCD-specific dependencies to your environment's `lib_deps`:

```ini
lib_deps = 
    bblanchon/ArduinoJson@^6.21.3
    ESPmDNS
    ; LCD UI dependencies (only if ENABLE_LCD_UI = 1)
    bodmer/TFT_eSPI@^2.5.43
    lvgl/lvgl@^8.3.11
    Wire
    SPI
```

Add LCD-specific build flags to your environment's `build_flags`:

```ini
build_flags =
    ; ... your existing flags ...
    ; LVGL configuration (only if ENABLE_LCD_UI = 1)
    -DLV_CONF_PATH="${PROJECT_SRC_DIR}/lv_conf.h"
    -DLV_LVGL_H_INCLUDE_SIMPLE=1
    ; Display configuration - JC2432W328C (ST7789 2.4" 240x320)
    -DUSER_SETUP_LOADED=1
    -DST7789_DRIVER=1
    -DTFT_WIDTH=240
    -DTFT_HEIGHT=320
    -DTFT_INVERSION_OFF=1
    ; Display pins (HSPI)
    -DTFT_MISO=12
    -DTFT_MOSI=13
    -DTFT_SCLK=14
    -DTFT_CS=15
    -DTFT_DC=2
    -DTFT_RST=-1
    -DTFT_BL=21
    ; Font loading
    -DLOAD_GLCD=1
    -DLOAD_FONT2=1
    -DLOAD_FONT4=1
    -DLOAD_FONT6=1
    -DLOAD_FONT7=1
    -DLOAD_FONT8=1
    -DLOAD_GFXFF=1
    -DSMOOTH_FONT=1
    ; SPI frequencies
    -DSPI_FREQUENCY=55000000
    -DSPI_READ_FREQUENCY=20000000
```

### 3. Flash and Run
```bash
pio run -e esp32dev --target upload
```

## Hardware Requirements
- JC2432W328C board with ESP32-D0WD-V3
- 2.4" 240x320 ST7789 TFT display (SPI)
- CST820 capacitive touch controller (I2C)
- Connections as per config.h pin definitions

## Memory Impact
When **disabled** (default):
- No flash or RAM overhead
- Code is completely compiled out via `#if ENABLE_LCD_UI`

When **enabled**:
- ~100KB additional flash
- ~50KB additional RAM (LVGL buffers + display framebuffer)
- 1 additional FreeRTOS task (low priority, ~4KB stack)

## Architecture
The LCD UI runs in a **separate low-priority FreeRTOS task** that:
- Does NOT interfere with critical timing task (priority 3)
- Uses DMA for non-blocking display updates
- Updates RSSI/lap count from main standalone mode loop
- Handles touch events and triggers callbacks for button presses

## Files Involved
- `src/config.h` - Enable/disable flag and pin configuration
- `src/lcd_ui.h/cpp` - LCD UI implementation (only compiled if enabled)
- `src/CST820.h/cpp` - Touch driver (only compiled if enabled)
- `src/lv_conf.h` - LVGL configuration (only used if enabled)
- `src/standalone_mode.h/cpp` - Integration with main app (conditional)

## Testing
A standalone test project is available in `testing/lcd_test/` for development and validation.

