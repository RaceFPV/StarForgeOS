# RTC6715 AGC_C Capacitor Fix

## Critical Hardware Issue: Wrong AGC_C Capacitor Value

### Symptoms

If your RTC6715 RSSI is:
- **Stuck at 255** (maximum) regardless of signal
- **Stuck at ~200** and doesn't change between channels
- **Only works when you touch the AGC_C capacitor** with your finger
- **Doesn't respond to frequency changes**

**Root Cause:** The AGC_C pin has the wrong capacitor value installed.

### The Problem

The RTC6715 AGC_C pin requires a capacitor to ground for proper AGC (Automatic Gain Control) loop timing. This capacitor creates an RC filter that controls how fast the AGC responds to signal changes.

**Common mistake:** Using a **6.8pF capacitor** (way too small!)

**Why 6.8pF fails:**
- AGC loop responds too fast → unstable
- AGC saturates → RSSI stuck at 255
- No proper filtering → poor signal tracking

**Why touching it works:**
- Your body adds ~100-200pF of capacitance
- This brings the total capacitance closer to the correct value
- Temporarily "fixes" the AGC timing

### The Solution

**Replace the capacitor with 1nF (1000pF)**

#### Recommended Capacitor:
- **Value:** 1nF (1000pF)
- **Type:** Ceramic capacitor (X7R or C0G/NP0)
- **Voltage:** 10V or higher (3.3V system, but 10V+ is safer)
- **Tolerance:** ±10% or ±20% is fine

#### Installation:
1. Remove the existing capacitor (likely 6.8pF or similar)
2. Solder 1nF capacitor:
   - One side → AGC_C pin on RTC6715
   - Other side → GND (ground plane)
3. Keep traces short (<10mm if possible)
4. Use good quality ceramic capacitor

### Alternative Values (if 1nF not available)

Try in this order:
1. **2.2nF** - Slightly slower AGC response (still works well)
2. **470pF** - Faster response (may be less stable)
3. **4.7nF** - Slower, more stable (less responsive)

**Note:** 1nF is the standard value used in most RX5808/RTC6715 modules and is recommended.

### Expected Results After Fix

✅ **Off-channel RSSI:** ~80-100 (noise floor)  
✅ **On-channel RSSI:** 150-220 (strong signal)  
✅ **RSSI changes** when switching frequencies  
✅ **RSSI responds** to signal strength changes  
✅ **No more stuck at 255**

### Real-World Test Results

After replacing 6.8pF with 1nF:
- **Off-channel:** RSSI = 99 ✅
- **On-channel (locked):** RSSI = 200 ✅
- **Best performance ever seen!**

### Technical Explanation

The AGC_C capacitor creates an RC time constant:
- **Too small (6.8pF):** AGC responds instantly → unstable → saturates
- **Correct (1nF):** AGC responds at proper rate → stable → accurate RSSI
- **Too large (10nF+):** AGC responds too slowly → poor tracking

The AGC loop needs time to:
1. Detect signal strength changes
2. Adjust gain appropriately
3. Filter out noise
4. Provide stable RSSI output

Without proper timing, the AGC can't function correctly.

### PCB Design Notes

If designing a new PCB:
- **Place capacitor close to AGC_C pin** (<5mm trace length)
- **Use ground plane** for capacitor ground connection
- **Avoid routing other signals** near AGC_C trace
- **Use 0402 or 0603 size** for easy assembly
- **Consider adding test point** for debugging

### Troubleshooting Checklist

If RSSI still doesn't work after capacitor fix:

- [ ] Verify capacitor is actually 1nF (measure with multimeter)
- [ ] Check capacitor is connected: AGC_C → cap → GND
- [ ] Verify no solder bridges or shorts
- [ ] Check RSSI pin connection to ESP32 ADC
- [ ] Verify ADC attenuation is set to 11dB (0-3.3V range)
- [ ] Check power supply is stable 3.3V
- [ ] Verify SPI mode is enabled (SPI_EN not grounded)

### References

- RTC6715 Datasheet - AGC_C pin specifications
- Standard RX5808 module schematics (typically use 1nF)
- This fix verified on StarForge hardware (Nov 2024)

---

**Last Updated:** November 2024  
**Verified Working:** ✅ Tested and confirmed on production hardware

