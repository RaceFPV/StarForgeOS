# StarForgeOS Testing Quick Start Guide

## Overview

This guide will help you quickly get started with testing your StarForgeOS firmware across multiple ESP32 board types to ensure compatibility.

## Prerequisites

1. **PlatformIO** installed:
   ```bash
   pip install platformio
   ```

2. **ESP32 board** (optional for build-only tests)

## Quick Test Commands

### 1. Build-Only Tests (No Hardware Required) ⚡

**Purpose**: Verify code compiles for all board types without needing physical hardware.

**Linux/Mac:**
```bash
cd StarForgeOS
./test/run_tests.sh build-only
```

**Windows:**
```cmd
cd StarForgeOS
test\run_tests.bat build-only
```

**Or run directly with PlatformIO:**
```bash
cd StarForgeOS
pio test -e test-esp32-c3 --without-uploading --without-testing
```

**What it does:**
- ✅ Verifies code compiles for all 7 board types
- ✅ Catches board-specific compilation errors
- ✅ Validates pin configurations are correct
- ⚡ Fast - runs in ~5-10 minutes

### 2. Hardware Tests (Board Required) 🔌

**Purpose**: Run actual tests on connected ESP32 hardware.

**Linux/Mac:**
```bash
cd StarForgeOS
./test/run_tests.sh hardware
```

**Windows:**
```cmd
cd StarForgeOS
test\run_tests.bat hardware
```

**Or run directly with PlatformIO:**
```bash
cd StarForgeOS
pio test -e test-esp32-c3  # Replace with your board
```

**What it does:**
- ✅ Uploads and runs tests on connected board
- ✅ Tests GPIO, ADC, WiFi, timing functions
- ✅ Verifies hardware works correctly
- 🔌 Requires board connected via USB

### 3. Test Specific Board 🎯

**Purpose**: Test only one board type (faster iteration).

**Linux/Mac:**
```bash
cd StarForgeOS
./test/run_tests.sh specific test-esp32-c3
```

**Windows:**
```cmd
cd StarForgeOS
test\run_tests.bat specific test-esp32-c3
```

**Or run directly with PlatformIO:**
```bash
cd StarForgeOS
pio test -e test-esp32-c3
```

**Available boards:**
- `test-esp32-c3` - ESP32-C3 SuperMini
- `test-esp32-c6` - ESP32-C6 DevKit
- `test-esp32` - Generic ESP32
- `test-esp32-s2` - ESP32-S2
- `test-esp32-s3` - ESP32-S3
- `test-esp32-s3-touch` - Waveshare ESP32-S3-Touch-LCD-2
- `test-jc2432w328c` - JC2432W328C LCD board

## Recommended Workflow

### When Making Code Changes 🛠️

1. **Before committing**: Run build-only tests
   ```bash
   ./run_tests.sh build-only
   ```
   This ensures your changes compile for all boards.

2. **After fixing issues**: Test on your hardware
   ```bash
   ./run_tests.sh specific test-esp32-c3  # Replace with your board
   ```

3. **Before releasing**: Run full CI pipeline
   - Push to GitHub - CI automatically tests all boards
   - Review build artifacts and test results

### Continuous Integration 🤖

Every commit triggers automatic testing:
- ✅ All 7 board types are built
- ✅ Compilation errors are caught immediately
- ✅ Firmware artifacts are saved for 30 days
- 📊 Test summary is generated

## Understanding Test Results

### Success ✅
```
========================================
Testing ESP32-C3 (test-esp32-c3)
========================================
test_board_identification    [PASSED]
test_rssi_pin_defined       [PASSED]
test_rx5808_pins_defined    [PASSED]
...
Summary: 15 Tests 0 Failures
✓ ESP32-C3 build passed
```

### Failure ❌
```
========================================
Testing ESP32-C6 (test-esp32-c6)
========================================
test_rssi_pin_defined       [FAILED]
  Expected 0, got 3

Summary: 15 Tests 1 Failure
✗ ESP32-C6 build failed
```

## Test Categories

### 📌 Hardware Tests (`test_hardware/`)
- Pin configurations
- ADC setup
- SPI for RX5808
- Mode switch
- Power button

### ⏱️ Timing Tests (`test_timing/`)
- RSSI reading
- Lap detection
- Frequency control
- Threshold management
- Thread safety

### 📡 WiFi Tests (`test_wifi/`)
- AP initialization
- IP assignment
- SSID generation
- Stability
- mDNS

### 🖥️ LCD Tests (`test_lcd/`)
- I2C bus
- Touch controller
- Backlight
- Battery monitoring
- Audio (ESP32 only)

## Common Issues & Solutions

### Issue: "PlatformIO not found"
**Solution:**
```bash
pip install platformio
# Or on some systems:
pip3 install platformio
```

### Issue: "Permission denied" (Linux/Mac)
**Solution:**
```bash
# Add yourself to dialout group
sudo usermod -a -G dialout $USER
# Log out and back in

# Or run with sudo (not recommended)
sudo ./run_tests.sh
```

### Issue: Tests timeout
**Solution:**
- Check USB cable (must support data, not just power)
- Try different USB port
- Reset board and retry
- Increase timeout in `platformio.ini`

### Issue: "Board not responding"
**Solution:**
1. Press and hold BOOT button
2. Press RESET button
3. Release RESET, then BOOT
4. Retry upload

### Issue: WiFi tests fail on ESP32-C3
**Solution:**
This is expected - single-core chips need special WiFi handling. The memory issue [[memory:9983069]] explains that WiFi.softAP() must be called before high-priority tasks start.

## Manual Test Commands

### Run specific test file:
```bash
cd StarForgeOS/test
pio test -e test-esp32-c3 -f test_hardware
```

### Run with verbose output:
```bash
pio test -e test-esp32-c3 -v
```

### Monitor serial output:
```bash
pio device monitor -e test-esp32-c3
```

### Clean and rebuild:
```bash
pio test -e test-esp32-c3 -c
```

## Integration with Development

### Pre-commit Hook (Recommended)

Create `.git/hooks/pre-commit`:
```bash
#!/bin/bash
cd StarForgeOS/test
./run_tests.sh build-only
```

Make it executable:
```bash
chmod +x .git/hooks/pre-commit
```

Now tests run automatically before each commit!

### VS Code Integration

Add to `.vscode/tasks.json`:
```json
{
  "version": "2.0.0",
  "tasks": [
    {
      "label": "Run All Tests",
      "type": "shell",
      "command": "cd StarForgeOS/test && ./run_tests.sh build-only",
      "group": "test"
    }
  ]
}
```

Run with: **Terminal > Run Task > Run All Tests**

## Next Steps

1. ✅ Run `./run_tests.sh build-only` to verify setup
2. ✅ Connect your ESP32 board
3. ✅ Run `./run_tests.sh specific <your-board>` to test hardware
4. ✅ Review test output and fix any issues
5. ✅ Set up pre-commit hook for automatic testing
6. ✅ Push to GitHub to trigger CI pipeline

## Need Help?

- 📖 See full documentation: `StarForgeOS/test/README.md`
- 🐛 Report issues on GitHub with test output
- 💬 Include board type and test command used

Happy testing! 🚀

