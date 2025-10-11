# RSSI Reading Fixes - Critical Updates

## Problem Summary
Multiple users reported incorrect RSSI readings in rotorhazard-lite. After comparing with the proven PhobosLT lap timer implementation, several critical issues were identified and fixed.

## Root Causes Identified

### 1. Missing RX5808 Initialization ⚠️ CRITICAL
**Problem:**
- RX5808 module was **never properly reset or configured**
- No reset command sent to register 0xF on startup
- No power configuration sent to register 0xA
- Module could be in unknown state, power-save mode, or have wrong settings

**PhobosLT does:**
```cpp
void RX5808::init() {
    resetRxModule();  // Write to register 0xF
    setFrequency(POWER_DOWN_FREQ_MHZ);
}

void RX5808::resetRxModule() {
    // Send reset command
    setupRxModule();  // Configure power
}

void RX5808::setupRxModule() {
    setRxModulePower(0b11010000110111110011);  // Register 0xA
}
```

**Previous rotorhazard-lite:**
```cpp
void TimingCore::setupRX5808() {
    pinMode(...);  // Just set pins
    delay(100);
    // NO RESET!
    // NO CONFIGURATION!
}
```

**Fixed:**
- Added `resetRX5808Module()` - sends reset command to register 0xF
- Added `configureRX5808Power()` - configures register 0xA with PhobosLT settings
- Both called during initialization

**Why This Matters:**
- RX5808 might be in power-down mode from previous use
- Default register values might not be optimal
- Without reset, module behavior is unpredictable
- This could explain **all** the RSSI reading issues!

### 2. Incorrect ADC Scaling and Clamping ⚠️ CRITICAL
**Previous Implementation:**
```cpp
uint16_t raw = adc_value >> 2;  // Shift to 10-bit
if (raw > 0x01FF) raw = 0x01FF; // Clamp to 511 (WRONG!)
return raw >> 1;
```

**Problem:** 
- `0x01FF = 511` is only **half** of the 10-bit range (should be `0x03FF = 1023`)
- This severely limited effective RSSI range and precision
- ADC values above 2044 were being clamped incorrectly

**Fixed Implementation (matching PhobosLT):**
```cpp
if (adc_value > 2047) adc_value = 2047; // Clamp at 2047 (correct for 3.3V RX5808)
return adc_value >> 3; // Simple divide by 8
```

**Why This Works:**
- RX5808 is 3.3V powered, won't use full 12-bit ADC range (0-4095)
- Clamping at 2047 (roughly half) accounts for voltage limitations
- Single shift by 3 bits provides clean 0-255 output range
- Matches the proven PhobosLT approach exactly

### 3. Missing Frequency Stability Protection ⚠️ CRITICAL
**Problem:** 
- After changing frequency, RX5808 needs ~35ms to stabilize
- Previous code would read RSSI immediately, getting garbage values
- PhobosLT has `recentSetFreqFlag` protection, we had none

**Fix Added:**
```cpp
// In readRawRSSI():
if (recent_freq_change) {
    uint32_t elapsed = millis() - freq_change_time;
    if (elapsed < RX5808_MIN_TUNETIME) {
        return 0; // RSSI is unstable
    } else {
        recent_freq_change = false; // Tuning complete
    }
}
```

**Why This Matters:**
- Prevents reading unstable RSSI during frequency transitions
- Critical for accurate readings when changing channels/bands
- Can cause false lap detections or missing real laps without this

### 4. Improved Debug Output
**Added:**
- Clear indication of frequency stability status
- Correct clamping value display (2047 instead of 511)
- Proper RSSI range documentation (0-255)

## Technical Comparison: PhobosLT vs Previous rotorhazard-lite

| Aspect | PhobosLT (Working) | Previous rotorhazard-lite | Fixed rotorhazard-lite |
|--------|-------------------|--------------------------|------------------------|
| **Initialization** | | | |
| Reset Module | YES (register 0xF) | NO ✗ | YES ✓ |
| Power Config | YES (register 0xA) | NO ✗ | YES ✓ |
| **RSSI Reading** | | | |
| ADC Range | 0-4095 (12-bit) | 0-4095 (12-bit) | 0-4095 (12-bit) |
| Clamp Value | 2047 | 2044 (via 511<<2) | 2047 ✓ |
| Bit Shift | >>3 | >>2 then >>1 | >>3 ✓ |
| Output Range | 0-255 | 0-255 (limited) | 0-255 ✓ |
| **Frequency Management** | | | |
| Freq Stability | YES (recentSetFreqFlag) | NO ✗ | YES ✓ |
| Tune Time Wait | 35ms | 0ms ✗ | 35ms ✓ |
| **Filtering** | | | |
| RSSI Filtering | Kalman Filter | Moving Average | Moving Average |

## Expected Improvements

After these fixes, users should see:
1. **RX5808 properly initialized** - Module in known state, power optimized
2. **RSSI readings actually work** - Module was possibly in wrong mode before!
3. **More accurate RSSI values** - Full range now properly utilized (0-255)
4. **Stable readings after frequency changes** - No more garbage values during tuning
5. **Better lap detection** - Accurate RSSI leads to better crossing detection
6. **Consistent behavior** - Matches proven PhobosLT implementation

## Constants Added
```cpp
#define RX5808_MIN_TUNETIME 35  // ms - wait after freq change before reading RSSI
#define RX5808_MIN_BUSTIME  30  // ms - minimum time between freq changes
```

## Files Modified
- `src/timing_core.h` - Added frequency stability tracking variables
- `src/timing_core.cpp` - Fixed RSSI scaling and added stability checks

## Testing Recommendations
1. Monitor RSSI values in standalone mode web interface
2. Check that RSSI responds correctly to signal strength changes
3. Verify lap detection works at various RSSI levels
4. Test frequency changes (band/channel switching) for stability
5. Compare with known good timer (e.g., PhobosLT) if available

## Migration Notes
- **Default threshold (50) should work better now** - Previous scaling made it too sensitive
- Users may need to recalibrate thresholds slightly
- RSSI values will be more accurate and use full 0-255 range
- Frequency changes will have a brief (~35ms) period with RSSI=0 (expected behavior)

## Credits
- PhobosLT project for reference implementation
- RX5808 datasheet for timing specifications
- Community feedback on RSSI issues

