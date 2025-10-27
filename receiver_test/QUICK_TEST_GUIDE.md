# Quick Test Guide - 10 Second Board Check

## Setup (Once)

1. Flash firmware: `pio run -e esp32-c3-supermini --target upload`
2. Open serial monitor: `pio device monitor -b 921600`
3. Set generator to **5800 MHz** (or 5725 MHz)

## Auto-Test (10 seconds per board) ⚡ FASTEST

**Just plug in board and watch!** The firmware auto-cycles between 5725 and 5800 MHz every 10 seconds.

### ✓ GOOD BOARD
```
[RSSI] 142 | Freq: 5800 MHz | AUTO-CYCLE: 5800 MHz  ← HIGH (matched!)
[RSSI] 145 | Freq: 5800 MHz | AUTO-CYCLE: 5800 MHz
→ AUTO-CYCLE: Switching to 5725 MHz
[RSSI] 23  | Freq: 5725 MHz | AUTO-CYCLE: 5725 MHz  ← LOW (off-freq)
[RSSI] 21  | Freq: 5725 MHz | AUTO-CYCLE: 5725 MHz
→ AUTO-CYCLE: Switching to 5800 MHz
[RSSI] 143 | Freq: 5800 MHz | AUTO-CYCLE: 5800 MHz  ← HIGH again!
```
**PASS** - RSSI changes with frequency → SPI works!

### ✗ BAD BOARD
```
[RSSI] 52  | Freq: 5800 MHz | AUTO-CYCLE: 5800 MHz  ← same
[RSSI] 53  | Freq: 5800 MHz | AUTO-CYCLE: 5800 MHz  ← same
→ AUTO-CYCLE: Switching to 5725 MHz
[RSSI] 54  | Freq: 5725 MHz | AUTO-CYCLE: 5725 MHz  ← same
[RSSI] 52  | Freq: 5725 MHz | AUTO-CYCLE: 5725 MHz  ← same
```
**FAIL** - RSSI doesn't change → Stuck in channel pin mode!

## Manual Test (30 seconds per board)

If you want to test manually, disable auto-cycle with `a` command.

### Step 1: Test on-frequency
```
f 5800
```
**Expected:** RSSI >100 (high signal)  
**If low:** Generator not working or wiring issue

### Step 2: Test off-frequency
```
f 5880
```
**Expected:** RSSI <50 (no signal)

### Step 3: Back to on-frequency
```
f 5800
```
**Expected:** RSSI >100 again

## Results

### ✓ GOOD BOARD
```
f 5800 → RSSI: 142  (HIGH)
f 5880 → RSSI: 23   (LOW)
f 5800 → RSSI: 145  (HIGH)
```
**PASS** - SPI frequency control works!

### ✗ BAD BOARD
```
f 5800 → RSSI: 52   (same)
f 5880 → RSSI: 54   (same)
f 5800 → RSSI: 53   (same)
```
**FAIL** - Stuck at one frequency (5725 MHz)  
**Diagnosis:** SPI_SE issue - chip in channel pin mode

## Quick Fix Verification

If you've modified SPI_SE (grounded it):

1. Flash test firmware to modified board
2. Run same test
3. Should now show GOOD BOARD behavior

## Batch Testing (AUTO MODE)

For multiple boards - **FASTEST METHOD:**
1. Flash all boards with test firmware
2. Set generator to 5800 MHz (or 5725 MHz)
3. Plug in board, watch for 10 seconds
4. If RSSI changes when frequency switches → GOOD
5. If RSSI stays constant → BAD
6. Sort into piles
7. **~10 seconds per board!**

## Batch Testing (Manual Mode)

If you prefer manual control:
1. Disable auto-cycle with `a` command
2. Test each one with manual procedure (30 sec each)
3. Sort into GOOD and BAD piles
4. All BAD boards need SPI_SE fix

## Alternative: Channel Mode Force Test

If you suspect channel pin mode but want software confirmation:
```
c
```
This forces pins LOW and resets chip. If in channel mode, switches to 5865 MHz.
**Pins stay LOW** for extended testing.

Set generator to 5865 MHz, then type `r` to read RSSI.
If RSSI goes HIGH → **channel pin mode confirmed!**

Try other frequencies (5800, 5732, etc.) with `r` to verify behavior.

When done, restore normal operation:
```
n
```

