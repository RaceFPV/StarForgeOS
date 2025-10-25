# Flash Memory Optimization Guide

## Current Situation

**ESP32-C3 Flash:** 1.25 MB (1,310,720 bytes)  
**Current Usage:** ~86.3% (1,131,200 bytes)  
**Available:** ~180 KB (179,520 bytes)

**RAM:** 320 KB (327,680 bytes)  
**Current Usage:** ~14.1% (46,320 bytes) - **EXCELLENT**  
**Available:** ~280 KB - plenty of headroom

## ⚠️ Problem

With only **180KB flash remaining**, adding touchscreen support and future features will be tight. We need to optimize!

## ✅ Solution 1: Move Web Assets to SPIFFS (DONE - Saves ~40KB)

### What Changed

Previously, CSS and JavaScript were **embedded in the binary** as giant strings in `standalone_mode.cpp`:

```cpp
// OLD METHOD - Wastes 40KB of flash
String css = R"(
  body { ... 15KB of CSS ... }
)";
_server.send(200, "text/css", css);

String js = R"(
  console.log(...); ... 25KB of JavaScript ...
)";
_server.send(200, "application/javascript", js);
```

Now they're **served from SPIFFS filesystem**:

```cpp
// NEW METHOD - Saves ~40KB flash!
File file = SPIFFS.open("/style.css", "r");
_server.streamFile(file, "text/css");
file.close();
```

### Benefits

- ✅ **~40KB flash saved** (CSS + JS moved to SPIFFS)
- ✅ Easier to update web interface (no recompile)
- ✅ Can add more web features without bloating binary
- ✅ Better separation of concerns

### Files Affected

- `src/standalone_mode.cpp` - Now serves from SPIFFS
- `data/style.css` - Already existed
- `data/app.js` - Already existed
- `data/index.html` - Already existed

### How to Upload SPIFFS

After compilation, upload filesystem:

```bash
pio run --target uploadfs
```

Or in VSCode PlatformIO: **Project Tasks > Platform > Upload Filesystem Image**

## 🔧 Solution 2: Add Compiler Optimization Flags (Easy - Saves ~50KB)

### Edit platformio.ini

Add these build flags:

```ini
[env:esp32-c3-supermini]
platform = espressif32
board = esp32-c3-supermini
framework = arduino

; Flash optimization flags
build_flags =
    -Os                      ; Optimize for size (instead of -O2 for speed)
    -DCORE_DEBUG_LEVEL=0     ; Disable debug logging in core libraries
    -DBOARD_HAS_PSRAM=0      ; ESP32-C3 has no PSRAM
    -ffunction-sections      ; Put each function in its own section
    -fdata-sections          ; Put each data item in its own section
    -Wl,--gc-sections        ; Linker: remove unused sections
    -Wl,--strip-all          ; Strip all symbols

; Remove unused Arduino libraries
build_unflags =
    -std=gnu++11
build_src_flags =
    -std=gnu++17
```

### Benefits

- ✅ **~50KB saved** through aggressive optimization
- ✅ Removes unused code/data
- ✅ No code changes needed
- ⚠️ Slightly slower execution (negligible for your use case)

## 🗑️ Solution 3: Conditional Compilation (Moderate - Saves ~30KB)

### Problem

Both **standalone mode** AND **node mode** are compiled into the binary even though you only use one at a time.

### Solution

Add conditional compilation flags:

```ini
[env:esp32-c3-standalone]
board = esp32-c3-supermini
build_flags =
    -DENABLE_STANDALONE_MODE
    -DDISABLE_NODE_MODE

[env:esp32-c3-node]
board = esp32-c3-supermini
build_flags =
    -DENABLE_NODE_MODE
    -DDISABLE_STANDALONE_MODE
```

Then in code:

```cpp
// main.cpp
#ifdef ENABLE_STANDALONE_MODE
  StandaloneMode standalone;
#endif

#ifdef ENABLE_NODE_MODE
  NodeMode node;
#endif
```

### Benefits

- ✅ **~30KB saved** per build
- ✅ Can create specialized firmware images
- ⚠️ Need to compile twice for both modes

## 📝 Solution 4: Remove Debug Strings (Easy - Saves ~10KB)

### Add Production Build Mode

```ini
[env:esp32-c3-production]
board = esp32-c3-supermini
build_flags =
    -DPRODUCTION_BUILD
    -DCORE_DEBUG_LEVEL=0
```

### Wrap Debug Code

```cpp
// config.h
#ifndef PRODUCTION_BUILD
  #define DEBUG_SERIAL 1
#else
  #define DEBUG_SERIAL 0  // Disable in production
#endif
```

All your `Serial.printf()` debug statements will be compiled out!

### Benefits

- ✅ **~10KB saved** from removed debug strings
- ✅ Faster execution (no serial overhead)
- ✅ More professional (no debug spam)
- ⚠️ Harder to debug issues in production

## 📊 Flash Savings Summary

| Optimization | Difficulty | Flash Saved | Done? |
|--------------|-----------|-------------|-------|
| **Move web assets to SPIFFS** | Easy | ~40KB | ✅ YES |
| **Compiler optimization** | Easy | ~50KB | ❌ TODO |
| **Conditional compilation** | Moderate | ~30KB | ❌ TODO |
| **Remove debug strings** | Easy | ~10KB | ❌ TODO |
| **TOTAL POTENTIAL** | - | **~130KB** | - |

## 🎯 Recommended Action Plan

### Phase 1: Quick Wins (Do Now)

1. ✅ **Web assets to SPIFFS** - DONE! (~40KB saved)
2. **Add compiler optimization flags** - 5 minutes, ~50KB saved
3. **Upload SPIFFS data** - `pio run --target uploadfs`

**Expected Result:** ~90KB saved, **~270KB free** (from 180KB)

### Phase 2: If Still Tight (Do Later)

4. **Add production build** - Disable debug in final releases (~10KB)
5. **Conditional compilation** - Separate standalone/node builds (~30KB)

**Expected Result:** ~130KB total saved, **~310KB free**

## 🧪 How to Measure

### Before Optimization

```bash
pio run
# Look for: Flash: [=========] 86.3% (1131200 bytes)
```

### After Each Change

```bash
pio run
# Look for improved percentage
```

### Detailed Analysis

```bash
pio run --target size
# Shows size of each compiled file
```

## 📱 Touchscreen Library Sizes

For reference, typical LCD/touchscreen library sizes:

| Library | Flash Usage | Notes |
|---------|-------------|-------|
| **TFT_eSPI** | 30-50KB | Optimized, recommended |
| **Adafruit_GFX** | 40-60KB | More features, heavier |
| **LovyanGFX** | 20-40KB | Very efficient, modern |
| **LVGL** | 150-200KB | Full GUI framework - TOO BIG |

**Recommendation:** Use **TFT_eSPI** or **LovyanGFX** for best balance.

## ⚡ Quick Fix Right Now

Add this to `platformio.ini`:

```ini
[env:esp32-c3-supermini]
platform = espressif32
board = esp32-c3-supermini
framework = arduino

build_flags =
    -Os
    -DCORE_DEBUG_LEVEL=0
    -ffunction-sections
    -fdata-sections
    -Wl,--gc-sections

monitor_speed = 921600
```

Then rebuild:

```bash
pio run -t clean
pio run
```

**Expected:** Flash usage drops to ~72% (~270KB free)

## 🔍 Troubleshooting

### "SPIFFS Mount Failed"

```bash
pio run --target uploadfs
```

Make sure `data/` folder contains:
- `index.html`
- `style.css`
- `app.js`

### "File Not Found" in Web UI

Check serial output:
```
SPIFFS mounted successfully
```

If not, filesystem wasn't uploaded.

### Flash Still Too High

1. Check what's using space:
   ```bash
   pio run --target size | grep -E "\.text|\.rodata"
   ```

2. Look for large objects:
   ```bash
   nm -S -r .pio/build/*/firmware.elf | head -20
   ```

3. Consider removing unused features

## 📈 Expected Final Result

After all optimizations:

```
Before: 86.3% flash used (180KB free)
After:  ~60% flash used (520KB free) ✅
```

**Plenty of room for:**
- ✅ Touchscreen support (30-50KB)
- ✅ More web features
- ✅ Additional protocols
- ✅ Future expansion

---

**Bottom Line:** With these optimizations, you'll have **3x more free flash** than you have now! 🚀

