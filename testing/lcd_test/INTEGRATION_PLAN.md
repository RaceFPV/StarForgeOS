# StarForge LCD Integration Plan

## Overview

This test demonstrates the JC2432W328C display working with verified pins. The goal is to integrate this display into the StarForge timing system as a local UI alternative/complement to the web interface.

## Verified Pin Configuration

### Display (ILI9341 via HSPI)
- **MISO**: GPIO 12
- **MOSI**: GPIO 13
- **SCLK**: GPIO 14
- **CS**: GPIO 15
- **DC**: GPIO 2
- **RST**: -1 (not used)
- **Backlight**: GPIO 21

### Touch (XPT2046 via VSPI)
- **MISO**: GPIO 39
- **MOSI**: GPIO 32
- **SCLK**: GPIO 25
- **CS**: GPIO 33
- **INT**: GPIO 36

## Current Test Features

The `lcd_test` displays a StarForge-themed UI with:
- **Header**: "STARFORGE" branding
- **RSSI Display**: Large color-coded number (green/yellow/red)
- **Threshold Bar**: Visual indicator of RSSI vs threshold
- **Frequency**: Current RX frequency
- **Lap Counter**: Number of laps detected
- **Status Indicator**: Ready/Crossing state
- **Simulated Updates**: Random RSSI with crossing detection

## Integration Options

### Option 1: Dual-UI Mode (Recommended)
Keep both web interface AND add LCD support:

**Advantages:**
- Web interface for initial setup/configuration
- LCD for live race display (no phone needed)
- Best of both worlds

**Implementation:**
1. Add display code to StarForge `src/display_mode.h/cpp`
2. Initialize display in `main.cpp` alongside WiFi
3. Display shows live RSSI from `timing_core`
4. Web interface remains for config

### Option 2: LCD-Only Mode
Replace web interface with LCD for "gate-mount" devices:

**Advantages:**
- Simpler code (no web server overhead)
- Lower power consumption
- Faster boot time
- Perfect for battery-powered gates

**Implementation:**
1. New mode: `MODE_LCD` alongside `MODE_STANDALONE` and `MODE_ROTORHAZARD`
2. No WiFi initialization
3. All UI on LCD with button/touch controls

### Option 3: Hybrid Mode
LCD for race display, web for configuration:

**Advantages:**
- Best for fixed installations
- Configure via web, race with LCD
- Touch screen for lap reset/threshold adjust

**Implementation:**
1. WiFi AP only for configuration
2. During race, WiFi off, LCD shows live data
3. Touch controls for basic operations

## Recommended Architecture

```
StarForgeOS/
├── src/
│   ├── main.cpp                 (mode selection)
│   ├── timing_core.cpp/h        (existing - RX5808 + RSSI)
│   ├── standalone_mode.cpp/h    (existing - web UI)
│   ├── node_mode.cpp/h          (existing - RotorHazard protocol)
│   └── display_mode.cpp/h       (NEW - LCD UI)
└── hardware variants:
    ├── ESP32-C3 (compact, WiFi-only)
    └── ESP32-D0WD (with LCD support)
```

### New Display Mode Class

```cpp
class DisplayMode {
public:
    void begin(TimingCore* timingCore);
    void process();  // Update display from timing data
    void showRSSI(uint8_t rssi);
    void showLap(LapData lap);
    void showStatus(const char* status);
    void handleTouch();  // Touch screen controls
private:
    TFT_eSPI tft;
    TimingCore* _timingCore;
    TouchController touch;
};
```

## Pin Conflict Analysis

**ESP32-C3 (Current StarForge):**
- RX5808: GPIO 3 (RSSI), 4 (CLK), 6 (DATA), 7 (SEL)
- Mode: GPIO 1

**ESP32-D0WD (LCD Board):**
- Display: GPIO 12, 13, 14, 15, 2, 21
- Touch: GPIO 25, 32, 33, 39
- **NO CONFLICTS** with RX5808 pins!

✅ **Can use both RX5808 and LCD on same ESP32-D0WD board**

## Implementation Steps

### Phase 1: Basic Display (DONE ✓)
- [x] Verify LCD pins
- [x] Basic TFT_eSPI test
- [x] StarForge UI mockup
- [x] Simulated RSSI updates

### Phase 2: Integration with TimingCore
- [ ] Add `display_mode.cpp/h` to StarForge
- [ ] Connect to `timing_core` data
- [ ] Real RSSI updates (not simulated)
- [ ] Real lap detection
- [ ] Threshold visualization

### Phase 3: Touch Controls
- [ ] Add touch library
- [ ] Calibration routine
- [ ] Touch buttons for:
  - Start/Stop race
  - Adjust threshold
  - Reset laps
  - Change frequency

### Phase 4: Advanced Features
- [ ] Lap time history (scrolling list)
- [ ] Best lap indicator
- [ ] Battery voltage display
- [ ] WiFi status/QR code for web UI

## Hardware Configuration

### Development Board (Current Test)
- **Board**: JC2432W328C
- **Chip**: ESP32-D0WD-V3
- **Display**: 2.4" ILI9341 (240x320)
- **Touch**: XPT2046 resistive
- **Power**: USB-C with battery charging

### Production Options

**Option A: JC2432W328C as-is**
- All-in-one solution
- Add RX5808 module externally
- Battery powered

**Option B: Custom PCB**
- ESP32-D0WD-V3
- Integrated RX5808
- ILI9341 display connector
- Optimized size/cost

## Usage Scenarios

### Scenario 1: Race Gate with Display
```
┌─────────────────────┐
│   STARFORGE         │
│                     │
│   RSSI: 127  [===] │
│                     │
│   Freq: 5800 MHz   │
│   Laps: 3          │
│   ● READY          │
└─────────────────────┘
```

### Scenario 2: Portable Checker
- Battery powered
- Handheld device
- Check VTX frequencies
- Verify RSSI levels
- Touch to cycle channels

### Scenario 3: Multi-Gate System
- Multiple gates with displays
- Central RotorHazard server
- LCD shows local gate status
- No phone needed at gate

## Next Steps

1. **Test This Firmware**:
   ```bash
   cd StarForgeOS/testing/lcd_test
   pio run --target upload --upload-port /dev/ttyUSB0
   ```

2. **Verify Display Works**: You should see:
   - Red/Green/Blue color flash
   - StarForge UI with animated RSSI
   - Simulated lap counting

3. **Connect RX5808** (if you have one):
   - Add RX5808 to same ESP32
   - Pins won't conflict
   - Test real RSSI display

4. **Choose Integration Option**: 
   - Dual-UI (recommended for versatility)
   - LCD-only (for simplicity)
   - Hybrid (for fixed installations)

## Questions to Consider

1. **Primary Use Case**: 
   - Fixed gates at track?
   - Portable checker?
   - Both?

2. **Power Source**:
   - USB powered?
   - Battery with charging?
   - Both?

3. **Touch Controls**:
   - Essential or nice-to-have?
   - What functions need touch?

4. **WiFi Usage**:
   - Configuration only?
   - Live web UI too?
   - RotorHazard node mode?

Let me know which option sounds best and I can help integrate it into the main StarForge codebase!

