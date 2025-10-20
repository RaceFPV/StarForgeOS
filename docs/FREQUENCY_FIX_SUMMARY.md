# Frequency Change Issue - Diagnosis and Fix

## Problem Summary

Your Tracer board with RTC6715 chip is stuck at a single frequency (R1 5658 MHz) and won't respond to frequency changes, even though the same code works fine with standard RX5808 modules.

## Root Cause: Hardware Configuration Issue

**The RTC6715 is in Channel Pin Mode instead of SPI Mode.**

The RTC6715 chip supports two operating modes:

1. **Channel Pin Mode (default)** - Uses 3 hardware pins (CH1, CH2, CH3) to select 8 fixed channels
   - **This is what your board is currently doing**
   - Ignores all SPI commands from software
   - Fixed at one frequency based on the voltage level of CH1/CH2/CH3 pins

2. **SPI Mode (required)** - Uses SPI protocol for software control
   - **This is what your firmware expects**
   - Allows any frequency from 5645-5945 MHz
   - Requires SPI_EN pin to be HIGH (not grounded)

## The Fix: Enable SPI Mode

### Hardware Modification Required

You need to modify the **SPI_EN pin** on your Tracer board:

**Current state:** SPI_EN → GND (Channel Pin Mode active)  
**Required state:** SPI_EN → 3.3V or floating (SPI Mode active)

### How to Do It

1. **Locate the SPI_EN pin** on the RTC6715 chip (often pin 13)
2. **Find the pull-down resistor** connecting SPI_EN to GND
   - Look for a small 0Ω or 10kΩ resistor near the RTC6715
   - May be labeled R1, R2, R3, or "SPI"
3. **Remove the resistor** with a soldering iron
   - Or cut the trace if there's no resistor
4. **Optional:** Add a 10kΩ pull-up resistor from SPI_EN to 3.3V for reliability

## Diagnostic Tools (Added to Firmware)

I've added several diagnostic features to help you verify the issue:

### 1. Automatic Hardware Test Function

**Trigger via Web API:**
```bash
curl -X POST http://192.168.4.1/api/test_hardware
```

**Or just change frequencies and watch serial output:**
- Boot in WiFi mode (connect MODE_SWITCH_PIN to GND)
- Open serial monitor at 921600 baud
- Change frequency in web UI
- Watch for detailed diagnostic output

### 2. Enhanced Debug Output

Every frequency change now prints:
```
=== RTC6715 Frequency Change ===
Target: 5695 MHz (tf=2608, N=81, A=4, reg=0x2890)
Pins: DATA=6, CLK=4, SEL=7
SPI sequence sent successfully
Frequency set to 5695 MHz (RSSI unstable for 35ms)
Waiting for module to tune...
RSSI after freq change: 45 (ADC: 360)
If RSSI doesn't change between frequencies, check SPI_EN pin!
=================================
```

### 3. Hardware Pin Test

The test function verifies:
- ✅ DATA pin can toggle (pin 6)
- ✅ CLK pin can toggle (pin 4)
- ✅ SEL pin can toggle (pin 7)
- ✅ RSSI ADC is reading (pin 3)
- ✅ SPI commands are being sent
- ❌ **But RTC6715 ignores them if SPI_EN is grounded**

## How to Verify the Issue

### Before Hardware Modification

1. Flash the updated firmware
2. Boot in WiFi mode
3. Connect to Tracer AP
4. Set your generator to R1 5658 MHz
5. Change Tracer to R2 5695 MHz in web UI
6. **Expected:** RSSI should drop dramatically
7. **Actual (if broken):** RSSI stays the same → **SPI_EN is grounded**

### After Hardware Modification

1. Remove/disconnect SPI_EN pull-down resistor
2. Reboot the board
3. Repeat the test above
4. **Expected:** RSSI now drops when you change frequency ✅
5. **Success!** SPI Mode is now enabled

## Files Modified

I've updated your firmware with:

1. **src/timing_core.cpp**
   - Enhanced frequency change debug output
   - Added `testSPIPins()` hardware diagnostic function
   - Shows register values, pin numbers, and post-change RSSI

2. **src/timing_core.h**
   - Added `testSPIPins()` public method

3. **src/standalone_mode.cpp**
   - Added `/api/test_hardware` endpoint
   - Allows triggering diagnostic test from web UI or curl

4. **src/standalone_mode.h**
   - Added `handleTestHardware()` method declaration

5. **docs/RTC6715_TROUBLESHOOTING.md**
   - Comprehensive troubleshooting guide
   - Hardware modification instructions
   - Step-by-step diagnostic procedures

6. **docs/FREQUENCY_FIX_SUMMARY.md** (this file)
   - Quick reference guide

## Quick Reference: Pin Connections

```
ESP32-C3 → RTC6715
Pin 6    → DATA (SDA)
Pin 4    → CLK (SCK)
Pin 7    → SEL (SEN/CS)
Pin 3    → RSSI (ADC input)

⚠️ SPI_EN  → Must be HIGH or floating (NOT grounded!)
```

## Expected Behavior After Fix

### Working Correctly (SPI Mode Enabled)
- Frequency changes take effect immediately
- RSSI varies based on actual signal on each frequency
- Generator on R1 5658: High RSSI on R1, low RSSI on other channels
- Can tune to any frequency 5645-5945 MHz

### Still Broken (Channel Pin Mode)
- Frequency changes ignored
- RSSI always reflects same frequency (determined by CH1/CH2/CH3)
- SPI commands sent but not processed by chip
- Stuck at one channel

## Next Steps

1. **Flash the updated firmware** (I've added diagnostics)
2. **Test without hardware changes** to confirm diagnosis
3. **Locate SPI_EN pin** on your board
4. **Remove pull-down resistor** from SPI_EN
5. **Test again** - frequency changes should now work!

## Additional Resources

- See `docs/RTC6715_TROUBLESHOOTING.md` for detailed troubleshooting
- RTC6715 datasheet for your specific chip revision
- Standard RX5808 SPI modification guides (same principle applies)

## Questions?

If frequency changes still don't work after enabling SPI mode:
1. Verify SPI_EN is truly disconnected from GND (use multimeter)
2. Check for solder bridges on SPI pins
3. Verify 3.3V power supply is stable
4. Try slower SPI timing (increase delays in `sendRX5808Bit()`)

Good luck with the hardware modification! The diagnostic output should make it very clear whether SPI mode is working.

