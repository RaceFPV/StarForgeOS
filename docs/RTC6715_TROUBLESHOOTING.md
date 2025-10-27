# RTC6715 Frequency Change Troubleshooting Guide

## Issue: StarForge Board Stuck at Single Frequency (R1 5658 MHz)

If your StarForge board won't change frequencies and stays locked at R1 5658 MHz (or any other single frequency), this is likely a **hardware configuration issue**, not a software bug.

## Root Cause: SPI Mode Not Enabled

The RTC6715 chip has two operating modes:

### 1. Channel Pin Mode (Default - WRONG for software control)
- Uses 3 hardware pins: CH1, CH2, CH3
- Selects 1 of 8 fixed channels based on pin states
- **Ignores SPI commands completely**
- This is why you're stuck at one frequency!

### 2. SPI Mode (Required for software control)
- Uses SPI protocol for frequency programming
- Allows any frequency from 5645-5945 MHz
- **This is what the StarForge firmware requires**

## How to Fix: Enable SPI Mode

### Step 1: Locate the SPI_EN Pin

The RTC6715 chip has an **SPI_EN pin** (sometimes labeled SPI_EN, SPE, or pin 13 on the chip).

**Check your board for:**
1. A small 0-ohm resistor or jumper near the RTC6715
2. Common labels: R1, R2, R3, "SPI", "SPE"
3. Often connects SPI_EN to GND (ground plane)

### Step 2: Enable SPI Mode

**Option A: Remove the Pull-Down Resistor**
- Locate the resistor connecting SPI_EN to GND
- Carefully remove it with a soldering iron
- The pin will float HIGH, enabling SPI mode

**Option B: Cut the Trace (if no resistor)**
- Some boards have a direct trace to GND
- Carefully cut the trace with a hobby knife
- Or drill through the via connecting to ground

**Option C: Add a Pull-Up Resistor (safest)**
- Add a 10kΩ resistor from SPI_EN to 3.3V
- This ensures SPI mode is reliably enabled

### Step 3: Verify Pin States

Check that channel pins (CH1, CH2, CH3) are:
- Pulled LOW (to GND) via resistors, OR
- Left floating/unconnected

These pins are only used in Channel Pin Mode and should be disabled.

## Hardware Verification Checklist

### Visual Inspection
- [ ] RTC6715 chip is properly soldered (no cold joints)
- [ ] All 3 SPI pins are connected: DATA, CLK, SEL
- [ ] No solder bridges between SPI pins
- [ ] RSSI output pin is connected to ESP32 ADC

### Multimeter Tests (Board Powered OFF)
- [ ] Check continuity from ESP32 DATA pin to RTC6715 SDA
- [ ] Check continuity from ESP32 CLK pin to RTC6715 SCK  
- [ ] Check continuity from ESP32 SEL pin to RTC6715 CS/SEN
- [ ] Verify SPI_EN is NOT connected to GND (should be open or 3.3V)

### Oscilloscope Tests (Board Powered ON - Advanced)
- [ ] Observe DATA line during frequency change - should see data pulses
- [ ] Observe CLK line - should see 25 clock pulses per command
- [ ] Observe SEL line - should go LOW during transmission

## Software Diagnostic

### Automatic Hardware Test

The firmware now includes an automated hardware test. You can trigger it in two ways:

**Method 1: Web API**
```bash
curl -X POST http://192.168.4.1/api/test_hardware
```

**Method 2: Serial Monitor**
- Boot in WiFi mode (MODE_SWITCH_PIN = LOW/GND)
- Open serial monitor at 921600 baud
- The test runs automatically on startup in debug mode

**Expected Output:**
```
=== RTC6715 Hardware Diagnostic ===
Testing SPI pin connections...

1. Testing DATA pin (should toggle HIGH/LOW):
   Pin 6: Toggle sent (use oscilloscope/LED to verify)

2. Testing CLK pin (should toggle HIGH/LOW):
   Pin 4: Toggle sent (use oscilloscope/LED to verify)

3. Testing SEL pin (should toggle HIGH/LOW):
   Pin 7: Toggle sent (use oscilloscope/LED to verify)

4. Testing RSSI input pin:
   Pin 3 (ADC): Raw ADC = 1245 (should be 0-4095)

5. Sending test SPI sequence to RTC6715:
   Attempting to set frequency to 5800 MHz...
   
=== RTC6715 Frequency Change ===
Target: 5800 MHz (tf=2660, N=83, A=4, reg=0x29C4)
Pins: DATA=6, CLK=4, SEL=7
SPI sequence sent successfully
...

=== Hardware Test Complete ===
```

### Manual Frequency Change Test

The updated firmware now includes detailed debug output. With WiFi mode enabled:

```
=== RTC6715 Frequency Change ===
Target: 5695 MHz (tf=2608, N=81, A=16, reg=0x2890)
Pins: DATA=6, CLK=4, SEL=7
SPI sequence sent successfully
Frequency set to 5695 MHz (RSSI unstable for 35ms)
Waiting for module to tune...
RSSI after freq change: 45 (ADC: 360)
If RSSI doesn't change between frequencies, check SPI_EN pin!
=================================
```

**Expected behavior:**
- RSSI should change significantly when switching frequencies
- If your generator is on R1 5658, switching to R2 5695 should drop RSSI to near 0

**Failure symptom:**
- RSSI stays the same regardless of frequency setting
- Indicates chip is in Channel Pin Mode (not responding to SPI)

## Testing Procedure

1. **Flash the updated firmware** with the new diagnostic code
2. **Boot in WiFi mode** (MODE_SWITCH_PIN = LOW/GND)
3. **Connect to SFOS WiFi AP**
4. **Open serial monitor** at 921600 baud
5. **Use the web interface** to change frequencies
6. **Observe the detailed debug output**

### Test with Signal Generator

1. Set generator to R1 5658 MHz
2. Position StarForge board near generator
3. In web UI, verify high RSSI on R1 (should be >100)
4. Switch to R2 5695 MHz in web UI
5. **Expected**: RSSI drops to ~0-30 (no signal)
6. **Failure**: RSSI stays high (stuck at R1)

If RSSI doesn't change = **Hardware issue, SPI not enabled**

## Common Issues

### Issue: "SPI sequence sent successfully" but no frequency change
**Solution:** SPI_EN pin is grounded. Remove pull-down resistor.

### Issue: Garbage RSSI values
**Solution:** Check RSSI pin connection and ADC configuration.

### Issue: Frequency changes work on standard RX5808 but not StarForge
**Solution:** Standard board has SPI enabled by default. StarForge needs modification.

### Issue: No debug output
**Solution:** Make sure you're in WiFi mode (not RotorHazard node mode).

## Pin Definitions Reference

From `config.h`:
```cpp
#define RSSI_INPUT_PIN      3     // ADC1_CH3 - RSSI from RTC6715
#define RX5808_DATA_PIN     6     // SPI MOSI to RTC6715 (SDA)
#define RX5808_CLK_PIN      4     // SPI SCK to RTC6715 (SCK)
#define RX5808_SEL_PIN      7     // SPI CS to RTC6715 (SEN/CS)
```

## Need More Help?

If you've verified all hardware connections and SPI mode is enabled but it still doesn't work:

1. **Check RTC6715 datasheet** for your specific chip revision
2. **Verify 3.3V power supply** is stable (check with multimeter)
3. **Try slower SPI timing** - increase delays in `sendRX5808Bit()`
4. **Check for hardware conflicts** - other devices on same pins?

## References

- RTC6715 Datasheet
- RX5808/RTC6715 SPI Protocol: 25-bit serial register write
- Standard RX5808 modules often have SPI_EN modification guides

