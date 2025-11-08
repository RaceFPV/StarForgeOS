# StarForgeOS Test Suite

Comprehensive test suite for StarForgeOS that ensures compatibility across multiple ESP32 board types.

## Supported Boards

This test suite validates the following ESP32 variants:

- **ESP32-C3** (single-core RISC-V, USB-CDC)
- **ESP32-C6** (WiFi 6, single-core RISC-V, USB-CDC)
- **ESP32** (dual-core Xtensa, Generic DevKit)
- **ESP32-S2** (single-core Xtensa, USB-CDC)
- **ESP32-S3** (dual-core Xtensa, PSRAM)
- **ESP32-S3-Touch** (Waveshare LCD board)
- **JC2432W328C** (ESP32 with LCD/Touch)

## Test Categories

### 1. Hardware Configuration Tests (`test_hardware/`)

Tests that verify board-specific pin configurations and hardware features:

- **Pin Configuration**: Verifies all GPIO pins are correctly defined for each board
- **ADC Configuration**: Tests RSSI input and battery monitoring (if applicable)
- **SPI Configuration**: Validates RX5808 control pins
- **I2C Configuration**: Tests touch controller I2C bus (LCD boards only)
- **Mode Switch**: Verifies mode selection mechanism (physical pin or LCD button)
- **Power Management**: Tests power button and deep sleep (if applicable)

**Run command:**
```bash
cd StarForgeOS/test
pio test -e test-esp32-c3 -f test_hardware
```

### 2. Timing Core Tests (`test_timing/`)

Tests the core timing functionality:

- **RSSI Reading**: Validates analog input from RX5808
- **RSSI Filtering**: Tests smoothing and noise reduction
- **Lap Detection**: Verifies crossing detection and lap recording
- **Peak/Nadir Tracking**: Tests RSSI extremum detection
- **Frequency Control**: Validates RX5808 frequency setting
- **Threshold Management**: Tests crossing threshold configuration
- **State Management**: Verifies reset and activation states
- **Concurrency**: Tests thread-safe operation with FreeRTOS

**Run command:**
```bash
cd StarForgeOS/test
pio test -e test-esp32-c3 -f test_timing
```

### 3. WiFi Tests (`test_wifi/`)

Tests WiFi functionality in standalone mode:

- **AP Initialization**: Tests WiFi Access Point startup
- **IP Configuration**: Verifies correct IP assignment
- **SSID Generation**: Tests unique SSID generation from MAC
- **Mode Switching**: Validates WiFi mode changes
- **Stability**: Tests WiFi reliability over time
- **Power Management**: Verifies WiFi sleep modes
- **mDNS**: Tests hostname configuration

**Run command:**
```bash
cd StarForgeOS/test
pio test -e test-esp32-c3 -f test_wifi
```

### 4. LCD/Touch Tests (`test_lcd/`)

Tests display and touch functionality (LCD boards only):

- **I2C Bus**: Tests touch controller communication
- **Backlight Control**: Validates display backlight (on/off and PWM)
- **Touch Controller**: Verifies CST816D/CST820 detection
- **Battery Monitoring**: Tests battery voltage reading (if applicable)
- **Audio**: Tests DAC output for beeps (ESP32 only, not S3)
- **Power Button**: Validates power management

**Run command:**
```bash
cd StarForgeOS/test
pio test -e test-esp32-s3-touch -f test_lcd
```

## Running Tests

### Prerequisites

1. Install PlatformIO:
   ```bash
   pip install platformio
   ```

2. Connect your ESP32 board via USB

### Running All Tests for a Specific Board

```bash
cd StarForgeOS/test
pio test -e test-esp32-c3
```

Replace `test-esp32-c3` with your board environment:
- `test-esp32-c3` - ESP32-C3 boards
- `test-esp32-c6` - ESP32-C6 boards
- `test-esp32` - Generic ESP32 boards
- `test-esp32-s2` - ESP32-S2 boards
- `test-esp32-s3` - ESP32-S3 boards
- `test-esp32-s3-touch` - Waveshare ESP32-S3-Touch-LCD-2
- `test-jc2432w328c` - JC2432W328C boards

### Running Specific Test Categories

```bash
cd StarForgeOS/test

# Run only hardware tests
pio test -e test-esp32-c3 -f test_hardware

# Run only timing tests
pio test -e test-esp32-c3 -f test_timing

# Run only WiFi tests
pio test -e test-esp32-c3 -f test_wifi

# Run only LCD tests (LCD boards only)
pio test -e test-esp32-s3-touch -f test_lcd
```

### Running Tests Without Hardware (Build Only)

```bash
cd StarForgeOS/test
pio test -e test-esp32-c3 --without-uploading --without-testing
```

This validates that the code compiles correctly for each board configuration.

## Continuous Integration

The test suite includes a GitHub Actions workflow (`.github/workflows/multi-board-tests.yml`) that automatically:

1. Builds firmware for all supported boards
2. Runs compilation tests to catch board-specific issues
3. Uploads firmware artifacts for each board
4. Generates a summary report

The CI pipeline runs on:
- Every push to `main` or `develop` branches
- Every pull request
- Manual workflow dispatch

## Test Results

Tests use the Unity test framework and produce detailed output:

```
Testing StarForgeOS Multi-Board Tests
======================================

test/test_hardware/test_pin_config.cpp:
    test_board_identification            [PASSED]
    test_rssi_pin_defined               [PASSED]
    test_rx5808_pins_defined            [PASSED]
    test_timing_constants               [PASSED]
    ...

Summary: 15 Tests 0 Failures
```

## Writing New Tests

### Test File Structure

Create new test files in the appropriate directory:

```
StarForgeOS/test/
├── test_hardware/
│   └── test_your_feature.cpp
├── test_timing/
├── test_wifi/
└── test_lcd/
```

### Example Test

```cpp
#include <Arduino.h>
#include <unity.h>
#include "../../src/config.h"

void test_your_feature(void) {
    // Arrange
    int expected = 42;
    
    // Act
    int actual = your_function();
    
    // Assert
    TEST_ASSERT_EQUAL(expected, actual);
}

void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_your_feature);
    UNITY_END();
}

void loop() {}
```

## Board-Specific Considerations

### Single-Core Boards (ESP32-C3, ESP32-C6, ESP32-S2)

- WiFi must be initialized **before** high-priority FreeRTOS tasks
- Timing task priority is higher to ensure accurate lap detection
- Serial communication may be affected by timing task load

### Dual-Core Boards (ESP32, ESP32-S3)

- Timing and web server can run concurrently on separate cores
- More resources available for concurrent operations

### LCD Boards (ESP32-S3-Touch, JC2432W328C)

- Mode selection via touchscreen instead of physical pin
- Battery monitoring requires ADC pin availability
- Audio only available on ESP32 (has built-in DAC), not ESP32-S3

## Troubleshooting

### Tests Fail to Upload

- Ensure correct USB port permissions: `sudo usermod -a -G dialout $USER`
- Try different upload speeds in `platformio.ini`
- Check USB cable quality (must support data, not just power)

### Tests Timeout

- Increase test timeout in `platformio.ini`: `test_timeout = 120`
- Check serial monitor for boot messages

### I2C/Touch Tests Fail

- Verify touch controller is properly connected
- Check I2C pullup resistors (typically 4.7kΩ)
- Scan I2C bus manually to verify address

### WiFi Tests Fail on Single-Core

- Ensure WiFi.softAP() is called early in setup()
- Reduce timing task priority temporarily for testing
- Check ESP-IDF version supports your board

## Contributing

When adding new features to StarForgeOS:

1. **Update Tests**: Add corresponding test cases for new functionality
2. **Test All Boards**: Run tests on all supported board types
3. **Document Changes**: Update this README with new test categories
4. **CI Pipeline**: Ensure GitHub Actions workflow passes

## License

Same license as StarForgeOS main project.

## Support

For issues or questions:
- Create an issue in the StarForgeOS repository
- Include test output and board type
- Describe steps to reproduce

