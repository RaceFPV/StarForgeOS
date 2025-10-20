# Tracer Test - RTC6715 Diagnostic Tool

Minimal test firmware for diagnosing RTC6715 chip and board issues.

## Purpose

This is a stripped-down version of the Tracer firmware with:
- ✓ Basic RTC6715 control
- ✓ RSSI reading
- ✓ Serial console interface
- ✗ No WiFi
- ✗ No web server
- ✗ No timing system
- ✗ No mode switching

Perfect for quickly testing if a board/chip is working correctly.

## Hardware

Same pinout as Tracer board:
- **GPIO 3** - RSSI input (ADC)
- **GPIO 6** - SPI DATA
- **GPIO 4** - SPI CLK
- **GPIO 7** - SPI SEL

## How to Use

### 1. Flash Firmware
```bash
cd tracer_test
pio run -e esp32-c3-supermini --target upload
```

### 2. Open Serial Monitor
```bash
pio device monitor -b 921600
```

Or use Arduino IDE serial monitor at 921600 baud.

### 3. Test Commands

**Toggle auto-cycle mode:**
```
a
```
Enables/disables automatic cycling between 5725 and 5800 MHz.
Default: ENABLED. Perfect for quick board testing!

**Set frequency:**
```
f 5800
```
Set manual frequency (disables auto-cycle).

**Read RSSI:**
```
r
```

**Reset chip:**
```
i
```

**Force channel mode test:**
```
c
```
Sets all pins LOW + resets chip. If in channel mode, forces to 5865 MHz.
**PINS STAY LOW** until you restore normal operation with `n`.

**Restore normal operation:**
```
n
```
Restores normal pin configuration after `c` command test.

**Show status:**
```
s
```
Shows current frequency, RSSI, and auto-cycle status.

**Help:**
```
h
```

## Testing Procedure

### Test 1: Verify SPI Mode Works

1. Set your generator to **5800 MHz**
2. Type: `f 5800`
3. RSSI should be **HIGH** (>100)
4. Type: `f 5658`
5. RSSI should drop **LOW** (<50)

**If RSSI changes:** ✓ SPI mode working!  
**If RSSI stays same:** ❌ Chip in channel pin mode!

### Test 2: Check Different Frequencies

Try multiple frequencies:
```
f 5658
f 5695
f 5732
f 5800
f 5880
```

Match your generator to each frequency and verify RSSI responds.

### Test 3: Force Channel Mode Test (Software)

1. Type: `c` (force channel mode test)
2. Command sets pins LOW and resets chip
3. If chip in channel mode → will switch to 5865 MHz
4. **Pins stay LOW for extended testing**
5. Set generator to 5865 MHz (or try other channels)
6. Type: `r` to read RSSI at different frequencies
7. If RSSI changes with generator frequency → chip is in channel mode
8. Type: `n` to restore normal operation when done

**Advantage:** No physical access needed, pure software test! Pins stay LOW for thorough testing.

### Test 4: Power Cycle Test (Hardware)

1. Set frequency: `f 5725`
2. Power off board
3. Ground all three SPI pins to GND
4. Power on board
5. Type: `r` to read RSSI
6. If RSSI changed → chip in channel pin mode (responds to pin states)

**Note:** Test 3 (`c` command) is easier, but Test 4 with physical power cycle is more definitive.

## Auto-Cycle Mode (Default)

**By default, the firmware automatically cycles between 5725 MHz and 5800 MHz every 10 seconds.**

This is perfect for quick board testing:
1. Set your signal generator to either 5725 MHz or 5800 MHz
2. Plug in the board
3. Watch the RSSI display
4. **Good board:** RSSI will be HIGH when frequency matches, LOW when it doesn't
5. **Bad board:** RSSI stays constant regardless of frequency

Example output with generator on 5800 MHz:
```
[RSSI] 142 | Freq: 5800 MHz | AUTO-CYCLE: 5800 MHz  ← HIGH (matched!)
[RSSI] 145 | Freq: 5800 MHz | AUTO-CYCLE: 5800 MHz  ← HIGH
→ AUTO-CYCLE: Switching to 5725 MHz
[RSSI] 23  | Freq: 5725 MHz | AUTO-CYCLE: 5725 MHz  ← LOW (off-frequency)
[RSSI] 21  | Freq: 5725 MHz | AUTO-CYCLE: 5725 MHz  ← LOW
→ AUTO-CYCLE: Switching to 5800 MHz
[RSSI] 143 | Freq: 5800 MHz | AUTO-CYCLE: 5800 MHz  ← HIGH (matched again!)
```

**Disable auto-cycle:** Type `a` or set manual frequency with `f <freq>`

## Manual RSSI Display

When auto-cycle is disabled, RSSI displays every second:
```
[RSSI] 142 | Freq: 5800 MHz
[RSSI] 139 | Freq: 5800 MHz
[RSSI] 145 | Freq: 5800 MHz
```

## Interpreting Results

### Good Board (SPI Mode Working)
```
→ Setting frequency to 5800 MHz...
  Formula: tf=2660, N=83, A=4, reg=0x29C4
  Sending SPI bits: 0001 1 0010 0110 0100 1001 0000
  SPI command sent
✓ Frequency set. Current RSSI: 142
```
RSSI changes with frequency → **SPI mode working!**

### Bad Board (Channel Pin Mode)
```
→ Setting frequency to 5800 MHz...
  Formula: tf=2660, N=83, A=4, reg=0x29C4
  Sending SPI bits: 0001 1 0010 0110 0100 1001 0000
  SPI command sent
✓ Frequency set. Current RSSI: 52
```
But RSSI stays at **52 regardless of frequency** → **Chip in channel pin mode!**

Stuck at 5725 MHz (52 RSSI when generator on 5658) indicates:
- Internal pull-ups forcing pins HIGH (111 = 5725 MHz)
- SPI_SE not enabling SPI mode
- Need to check SPI_SE connection

## Comparing Boards

Flash same firmware to multiple boards and compare:
- **Good board:** RSSI tracks frequency changes
- **Bad board:** RSSI stays constant

This quickly identifies which boards have the SPI_SE issue.

## Serial Output Example

```
╔════════════════════════════════════════════════════╗
║      TRACER TEST - RTC6715 Diagnostic Tool        ║
╚════════════════════════════════════════════════════╝

Minimal firmware for testing RTC6715 chips
Type 'h' for help

Initializing RTC6715...
  Sending reset command (register 0xF)...
  Reset complete
  Configuring power register (0xA)...
  Power configuration complete
→ Setting frequency to 5800 MHz...
  Formula: tf=2660, N=83, A=4, reg=0x29C4
  Sending SPI bits: 0001 1 0010 0110 0100 1001 0000
  SPI command sent
✓ Initialization complete

═══════════════════════════════════════════════════
CURRENT STATUS:
═══════════════════════════════════════════════════
  Frequency:  5800 MHz
  RSSI:       45
  Uptime:     3 seconds

  Pin Configuration:
    RSSI:  GPIO 3 (ADC)
    DATA:  GPIO 6
    CLK:   GPIO 4
    SEL:   GPIO 7
═══════════════════════════════════════════════════

[RSSI] 45 | Freq: 5800 MHz
[RSSI] 44 | Freq: 5800 MHz
```

## Troubleshooting

**No serial output:**
- Check USB cable
- Try different USB port
- Verify 921600 baud rate

**RSSI always 0:**
- Check RSSI pin connection (GPIO 3)
- Verify RTC6715 is powered
- Try reset command: `i`

**Frequency doesn't change:**
- Check if SPI_SE is properly connected
- Measure SPI_SE voltage (should be 3.3V OR 0V, not floating)
- Try grounding SPI_SE if currently at 3.3V

## Quick Reference

| Command | Description |
|---------|-------------|
| `f 5800` | Set freq to 5800 MHz |
| `r` | Read RSSI |
| `i` | Reset chip |
| `c` | Force channel mode test (pins LOW + reset) |
| `s` | Show status |
| `h` | Help |

## Files

- `src/main.cpp` - Single-file test firmware
- `platformio.ini` - PlatformIO configuration
- `README.md` - This file

