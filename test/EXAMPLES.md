# StarForgeOS Test Suite - Example Usage

## Real-World Example: Making a Code Change

Let's walk through a realistic scenario of making a code change to StarForgeOS and using the test suite to ensure it works across all boards.

### Scenario: Adding a New Frequency Band

You want to add support for a new frequency band to the RX5808 receiver.

---

## Step 1: Make the Code Change

Edit `StarForgeOS/src/config.h`:

```cpp
// Before
#define MIN_FREQ            5645  // Minimum frequency (MHz)
#define MAX_FREQ            5945  // Maximum frequency (MHz)

// After - extending range
#define MIN_FREQ            5600  // Minimum frequency (MHz) - EXTENDED
#define MAX_FREQ            6000  // Maximum frequency (MHz) - EXTENDED
```

---

## Step 2: Run Build-Only Tests (No Hardware Needed)

Open terminal and run:

```bash
cd StarForgeOS/test
./run_tests.sh build-only
```

**Expected Output:**
```
========================================
StarForgeOS Multi-Board Test Suite
========================================

Running BUILD-ONLY tests (no hardware required)
⚠ This validates compilation but does not run tests on hardware

========================================
Testing ESP32-C3 (test-esp32-c3)
========================================
test_frequency_range             [PASSED]
test_timing_constants            [PASSED]
...
Summary: 15 Tests 0 Failures
✓ ESP32-C3 build passed

========================================
Testing ESP32-C6 (test-esp32-c6)
========================================
✓ ESP32-C6 build passed

========================================
Testing ESP32 (test-esp32)
========================================
✓ ESP32 build passed

... (all boards pass)

========================================
Test Summary
========================================
Total boards tested: 7
✓ Passed: 7
✓ All tests passed!
```

✅ **Success!** Your change compiles on all 7 board types.

---

## Step 3: Test on Physical Hardware

Connect your ESP32-C3 board via USB and run:

```bash
./run_tests.sh specific test-esp32-c3
```

**Expected Output:**
```
========================================
Testing ESP32-C3 (test-esp32-c3)
========================================

Uploading firmware...
Writing at 0x00010000... (100%)

Running tests...

test_frequency_range:
  Frequency range: 5600 - 6000 MHz (default: 5800)
  [PASSED]

test_frequency_bounds:
  Testing MIN_FREQ (5600)... [OK]
  Testing MAX_FREQ (6000)... [OK]
  [PASSED]

test_frequency_set_get:
  Setting to 5600... [OK]
  Setting to 6000... [OK]
  [PASSED]

Summary: 18 Tests 0 Failures
✓ ESP32-C3 tests passed
```

✅ **Success!** Hardware tests pass on ESP32-C3.

---

## Step 4: Commit Your Changes

```bash
git add src/config.h
git commit -m "Extend frequency range to 5600-6000 MHz"
git push origin feature/extended-frequency
```

---

## Step 5: CI Automatically Tests All Boards

GitHub Actions runs automatically when you push:

```
Workflow: StarForgeOS Multi-Board Tests

✅ test-esp32-c3 (2m 15s)
✅ test-esp32-c6 (2m 18s)
✅ test-esp32 (2m 10s)
✅ test-esp32-s2 (2m 22s)
✅ test-esp32-s3 (2m 30s)
✅ test-esp32-s3-touch (2m 45s)
✅ test-jc2432w328c (2m 28s)

All checks passed!
```

---

## Step 6: Review PR and Merge

Your teammate reviews the PR and sees:
- ✅ All 7 boards build successfully
- ✅ Tests pass on all configurations
- ✅ Firmware artifacts available for download
- ✅ Test summary shows no regressions

**Merge with confidence!** 🎉

---

## Example 2: Breaking Change Detection

Now let's see what happens when you make a mistake.

### Oops: Wrong Pin for ESP32-C6

You accidentally use the wrong RSSI pin for ESP32-C6:

```cpp
// In config.h
#elif defined(CONFIG_IDF_TARGET_ESP32C6)
    #define RSSI_INPUT_PIN      3     // WRONG! Should be 0
```

Run tests:

```bash
./run_tests.sh build-only
```

**Output:**
```
========================================
Testing ESP32-C6 (test-esp32-c6)
========================================

test_rssi_pin_defined:
  Expected GPIO0 for ESP32-C6, got GPIO3
  [FAILED]

✗ ESP32-C6 build failed

========================================
Test Summary
========================================
Total boards tested: 7
✓ Passed: 6
✗ Failed: 1

Failed boards:
  - ESP32-C6
```

❌ **Tests caught the error!** Fix it before committing:

```cpp
#define RSSI_INPUT_PIN      0     // Fixed!
```

Re-run tests:

```bash
./run_tests.sh build-only
```

✅ **All pass now!** Safe to commit.

---

## Example 3: Testing Only LCD Boards

You made changes to the LCD UI code and want to test just the display boards:

```bash
# Test ESP32-S3-Touch
./run_tests.sh specific test-esp32-s3-touch

# Test JC2432W328C
./run_tests.sh specific test-jc2432w328c
```

Or run both in sequence:

```bash
for board in test-esp32-s3-touch test-jc2432w328c; do
    echo "Testing $board..."
    ./run_tests.sh specific $board
done
```

---

## Example 4: Debugging a Test Failure

A test fails on ESP32-S3-Touch:

```bash
./run_tests.sh specific test-esp32-s3-touch
```

**Output:**
```
test_i2c_bus_scan:
  Scanning I2C bus...
  No I2C devices found
  [FAILED]
```

**Debugging steps:**

1. Check connections:
   ```bash
   # Monitor serial output
   cd StarForgeOS/test
   pio device monitor -e test-esp32-s3-touch
   ```

2. Run with verbose output:
   ```bash
   pio test -e test-esp32-s3-touch -v
   ```

3. Check specific test:
   ```bash
   # Run only I2C tests
   pio test -e test-esp32-s3-touch -f test_lcd
   ```

4. Verify I2C pins in config:
   ```cpp
   // In config.h
   #define LCD_I2C_SDA     48    // Correct for ESP32-S3-Touch
   #define LCD_I2C_SCL     47    // Correct for ESP32-S3-Touch
   ```

---

## Example 5: Pre-Commit Hook

Set up automatic testing before commits:

```bash
# Create pre-commit hook
cat > .git/hooks/pre-commit << 'EOF'
#!/bin/bash
echo "Running StarForgeOS tests..."
cd StarForgeOS/test
./run_tests.sh build-only

if [ $? -ne 0 ]; then
    echo "❌ Tests failed! Fix errors before committing."
    exit 1
fi

echo "✅ Tests passed! Proceeding with commit."
EOF

# Make it executable
chmod +x .git/hooks/pre-commit
```

Now tests run automatically:

```bash
git commit -m "Update config"
```

**Output:**
```
Running StarForgeOS tests...
✓ ESP32-C3 build passed
✓ ESP32-C6 build passed
✓ ESP32 build passed
✓ ESP32-S2 build passed
✓ ESP32-S3 build passed
✓ ESP32-S3-Touch build passed
✓ JC2432W328C build passed
✅ Tests passed! Proceeding with commit.

[feature/update abc1234] Update config
 1 file changed, 2 insertions(+), 2 deletions(-)
```

---

## Example 6: Download Firmware from CI

After pushing to GitHub, download pre-built firmware:

1. Go to GitHub Actions tab
2. Click on latest workflow run
3. Scroll to "Artifacts" section
4. Download firmware for your board:
   - `firmware-esp32-c3.bin`
   - `firmware-esp32-s3-touch.bin`
   - etc.

5. Flash to board:
   ```bash
   # Using esptool
   esptool.py --port /dev/ttyUSB0 write_flash 0x10000 firmware-esp32-c3.bin
   
   # Or using PlatformIO
   pio run -e esp32-c3-supermini -t upload
   ```

---

## Example 7: Testing a New Feature

You're adding battery monitoring for a new board. Write a test first:

**Create `test/test_hardware/test_battery.cpp`:**

```cpp
#include <Arduino.h>
#include <unity.h>
#include "../../src/config.h"

void test_battery_monitoring(void) {
    #if ENABLE_BATTERY_MONITOR
        pinMode(BATTERY_ADC_PIN, INPUT);
        int raw = analogRead(BATTERY_ADC_PIN);
        
        TEST_ASSERT_TRUE(raw >= 0);
        TEST_ASSERT_TRUE(raw <= 4095);
        
        float voltage = (raw / 4095.0) * 3.3 * BATTERY_VOLTAGE_DIVIDER;
        TEST_ASSERT_TRUE(voltage >= 0.0);
        TEST_ASSERT_TRUE(voltage <= 5.0);
    #else
        TEST_PASS_MESSAGE("Battery monitoring not enabled");
    #endif
}

void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_battery_monitoring);
    UNITY_END();
}

void loop() {}
```

Run the test:

```bash
./run_tests.sh specific test-esp32-s3-touch
```

If it passes, implement the feature. If it fails, fix the implementation.

---

## Example 8: Continuous Monitoring

Set up continuous testing during development:

```bash
# Watch for file changes and auto-test
while true; do
    inotifywait -e modify src/*.cpp src/*.h
    ./run_tests.sh build-only
    sleep 2
done
```

Or use a smarter tool like `entr`:

```bash
ls src/*.cpp src/*.h | entr ./run_tests.sh build-only
```

---

## Common Commands Cheat Sheet

```bash
# Quick validation (no hardware)
./run_tests.sh build-only

# Test your board
./run_tests.sh specific test-esp32-c3

# Test all boards with hardware (slow!)
./run_tests.sh hardware

# Test specific category
pio test -e test-esp32-c3 -f test_hardware

# Verbose output
pio test -e test-esp32-c3 -v

# Clean and rebuild
pio test -e test-esp32-c3 -c

# Monitor serial output
pio device monitor -e test-esp32-c3

# List available environments
pio test --list-environments
```

---

## Tips for Success

1. **Always run build-only tests before committing**
   - Fast (5-10 minutes)
   - Catches most issues
   - No hardware needed

2. **Test on physical hardware after significant changes**
   - Validates actual behavior
   - Catches runtime issues
   - Verifies pin configurations

3. **Let CI do the heavy lifting**
   - Tests all boards automatically
   - Provides firmware artifacts
   - Gives confidence in PRs

4. **Use pre-commit hooks**
   - Prevents bad commits
   - Automated testing
   - Saves review time

5. **Keep tests up to date**
   - Add tests for new features
   - Update tests when behavior changes
   - Remove obsolete tests

---

## Next Steps

1. ✅ Try the examples above
2. ✅ Set up pre-commit hook
3. ✅ Run tests on your board
4. ✅ Review CI results on GitHub
5. ✅ Write a test for your next feature

Happy testing! 🚀

