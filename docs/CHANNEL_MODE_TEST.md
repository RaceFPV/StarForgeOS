# Channel Pin Mode Diagnostic Test

## Quick Test to Determine Operating Mode

This test definitively determines if your RTC6715 is in **Channel Pin Mode** (broken) or **SPI Mode** (correct).

## How It Works

The RTC6715 pins serve dual purposes:
- **In SPI Mode**: DATA, CLK, SEL = SPI communication pins
- **In Channel Pin Mode**: CH1, CH2, CH3 = Channel select pins

By setting all 3 pins HIGH, we can test which mode is active:
- **Channel Pin Mode**: CH1=1, CH2=1, CH3=1 → Forces frequency to 5725 MHz
- **SPI Mode**: Ignores pin states, stays at SPI-programmed frequency

## Running the Test

### Method 1: Web API (Easiest)

```bash
curl -X POST http://192.168.4.1/api/test_channel_mode
```

Then check serial monitor for results.

### Method 2: Serial Monitor

1. Boot in WiFi mode (MODE_SWITCH_PIN to GND)
2. Open serial monitor at 921600 baud
3. The test shows detailed analysis

## Understanding the Results

### Result 1: RSSI Changes (❌ BROKEN)

```
❌ RSSI CHANGED SIGNIFICANTLY!
   Before: 52 -> After: 23 (difference: 29)

DIAGNOSIS: RTC6715 is in CHANNEL PIN MODE
```

**What this means:**
- Chip is in Channel Pin Mode (NOT SPI Mode)
- Ignoring all SPI commands from your firmware
- Using pin voltage levels to select channels
- **SPI_EN pin is NOT at 3.3V** despite what schematic says

**Why this happens:**
- Manufacturing defect (trace not connected)
- Cold solder joint on SPI_EN pin
- Wrong component variant (defaults to pin mode)
- Schematic error (shows 3.3V but not actually connected)

**Fix:**
1. Verify SPI_EN voltage with multimeter:
   - Power on board
   - Measure SPI_EN to GND
   - Should read 3.3V
   - If it reads 0V or floating → Found the problem!

2. Fix the connection:
   - Add jumper wire from SPI_EN to 3.3V rail
   - Reflow solder joint if cold
   - Check for broken trace under microscope

### Result 2: RSSI Stable (✓ CORRECT MODE, WRONG PROTOCOL)

```
✓ RSSI REMAINED STABLE
   Before: 52 -> After: 54 (difference: 2)

DIAGNOSIS: RTC6715 is in SPI MODE (Correct!)
```

**What this means:**
- Chip is in SPI Mode ✓
- SPI_EN is correctly at 3.3V ✓  
- Pin states are being ignored ✓
- **But frequency changes still don't work**

**The problem is software/protocol:**
- Wrong SPI protocol or timing
- Wrong frequency calculation formula
- Wrong register layout
- Missing initialization sequence

**Next steps - Check datasheet for:**

1. **Frequency Register Formula**
   - Current code uses: `N = (freq - 479) / 64`, `A = ((freq - 479) % 64) / 2`
   - Does RTC6715 use the same formula?
   - Some chips use `(freq - 479) / 2` instead

2. **Register Layout**
   - Current: 4-bit address (0x1), 1-bit write flag, 16-bit data, 4-bit padding
   - Does RTC6715 use same 25-bit format?
   - Check bit order (LSB first vs MSB first)

3. **Initialization Sequence**
   - Does chip need special power-up sequence?
   - Different reset procedure?
   - Different power register values?

4. **SPI Timing**
   - Current: 300µs delays between bits
   - Try slower: 500µs or 1000µs
   - Try faster: 100µs

## Test Output Example

```
╔════════════════════════════════════════════════════════════════╗
║          RTC6715 CHANNEL PIN MODE TEST                        ║
╚════════════════════════════════════════════════════════════════╝

This test determines if RTC6715 is in Channel Pin Mode or SPI Mode
by setting all 3 pins HIGH, which forces channel select in pin mode.

Step 1: Setting frequency to 5658 MHz via SPI...
   RSSI at 5658 MHz: 52 (ADC: 425)

Step 2: Setting all 3 SPI pins HIGH...
   In Channel Pin Mode, this selects:
   CH1=1, CH2=1, CH3=1 = binary 111 = Channel 7/8
   Typically: 5725 MHz (Boscam A8)

   All pins set HIGH. Waiting for chip to respond...
   RSSI with pins HIGH: 54 (ADC: 437)

═══════════════════════════════════════════════════════════════
RESULT ANALYSIS:
═══════════════════════════════════════════════════════════════

✓ RSSI REMAINED STABLE
   Before: 52 -> After: 54 (difference: 2)

DIAGNOSIS: RTC6715 is in SPI MODE (Correct!)
[... detailed analysis ...]

Step 3: Restoring normal SPI operation...
   Pins restored. Test complete.
```

## Quick Reference

### Channel Pin Mode Frequency Table

If your chip IS in Channel Pin Mode, here's what the pins select:

| CH1 | CH2 | CH3 | Binary | Freq (MHz) | Band  |
|-----|-----|-----|--------|------------|-------|
| 0   | 0   | 0   | 000    | 5865       | A1    |
| 1   | 0   | 0   | 001    | 5845       | A2    |
| 0   | 1   | 0   | 010    | 5825       | A3    |
| 1   | 1   | 0   | 011    | 5805       | A4    |
| 0   | 0   | 1   | 100    | 5785       | A5    |
| 1   | 0   | 1   | 101    | 5765       | A6    |
| 0   | 1   | 1   | 110    | 5745       | A7    |
| 1   | 1   | 1   | 111    | 5725       | A8    |

*Note: Your board's default state (all floating or certain pins pulled down) might be why you're stuck at 5658 MHz (R1).*

## Additional Diagnostics

If the test shows you're in SPI Mode but frequency changes don't work, try:

### Test 1: Verify SPI Communication

Watch serial output when changing frequency:
```
Sending bits: 0001 1 0001 0010 0011 0100 0000
```

This shows the exact bits being sent. Compare to datasheet.

### Test 2: Try Manual Frequency Values

In the web UI, try these specific test frequencies:
- **5658 MHz** (R1) - Your current stuck frequency  
- **5725 MHz** (A8) - Easy channel select value
- **5800 MHz** (F4) - Middle of band
- **5880 MHz** (R7) - High end

Watch RSSI response to each. If RSSI never changes, protocol is wrong.

### Test 3: Oscilloscope Verification (Advanced)

Connect scope to SPI pins during frequency change:
- **DATA pin**: Should see data pulses
- **CLK pin**: Should see 25 clock cycles
- **SEL pin**: Should go LOW during transmission, HIGH after

If you don't see these signals, problem is earlier in the chain.

## Summary Decision Tree

```
Start Test
    ↓
Set all pins HIGH
    ↓
RSSI changed? ──YES→ Channel Pin Mode ──→ Fix SPI_EN hardware
    ↓
   NO
    ↓
SPI Mode (correct) ──→ Check datasheet for protocol differences
```

## Files Modified

This test was added to:
- `src/timing_core.h` - Added `testChannelPinMode()` method
- `src/timing_core.cpp` - Implemented the test logic
- `src/standalone_mode.h` - Added web handler
- `src/standalone_mode.cpp` - Added `/api/test_channel_mode` endpoint

## See Also

- `RTC6715_TROUBLESHOOTING.md` - Comprehensive troubleshooting guide
- `FREQUENCY_FIX_SUMMARY.md` - Overview of the issue
- `SPI_EN_MODIFICATION.txt` - Hardware modification guide (if needed)

