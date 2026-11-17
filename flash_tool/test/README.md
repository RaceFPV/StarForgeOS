# Flash Tool Testing Guide

This directory contains test utilities for testing the StarForge Flash Tool on headless Linux servers with real hardware.

## Overview

The flash tool is an Electron GUI application. These scripts exercise flashing against a real ESP32 on a machine that has the board plugged in.

## Test Setup

### Prerequisites

1. **Virtual Display (Xvfb)** - Required for running Electron headlessly
   ```bash
   sudo apt-get install xvfb
   ```

2. **Node.js and npm** - Already required for the flash tool
   ```bash
   node --version  # Should be 18+
   ```

3. **esptool** - For communicating with ESP32 devices
   ```bash
   pip install esptool
   # or
   sudo apt-get install esptool
   ```

4. **Serial Port Permissions**
   ```bash
   sudo usermod -a -G dialout $USER
   # Log out and back in for changes to take effect
   ```

5. **Hardware Setup**
   - Connect ESP32 boards to server via USB
   - Ensure boards are powered and in flash mode (if needed)

### Quick Start

The easiest way to run tests is using the convenience script:

```bash
cd flash_tool
chmod +x test/run-headless-tests.sh
./test/run-headless-tests.sh
```

This script will:
- Check dependencies
- Set up a virtual display (Xvfb)
- Run the test suite
- Clean up automatically

## Test Files

### `flash-test-suite.js`
Comprehensive test suite that:
- Detects serial ports and ESP32 devices
- Validates firmware files
- Tests chip detection
- Verifies Electron can run in headless mode

**Usage:**
```bash
# Set up display first
export DISPLAY=:99
Xvfb :99 -screen 0 1024x768x24 &

# Run tests
node test/flash-test-suite.js
```

**Environment Variables:**
- `TEST_PORT` - Serial port to use (default: auto-detect)
- `TEST_BOARD` - Board type (default: auto-detect from chip)
- `TEST_FIRMWARE_DIR` - Directory with test firmware (default: `./test-firmware`)
- `TEST_TIMEOUT` - Test timeout in ms (default: 60000)
- `DISPLAY` - X display to use (default: `:99`)

### `headless-test-runner.js`
Lower-level test runner for direct function testing.

**Usage:**
```bash
# List available serial ports
node test/headless-test-runner.js --list-ports

# Detect chip type
node test/headless-test-runner.js --detect-chip --port /dev/ttyUSB0

# Test firmware files
node test/headless-test-runner.js --port /dev/ttyUSB0 --board esp32-c3 --firmware ./firmware/
```

## Running on a machine with a board

These checks need a serial port, an ESP32, and a `firmware.bin`. GitHub-hosted runners have none of those, so they are not part of CI. The Mac and Windows app builds run in `.github/workflows/build-flasher.yml`.

### Local server setup

1. **Install dependencies on server:**
   ```bash
   sudo apt-get update
   sudo apt-get install -y xvfb nodejs npm python3-pip
   pip3 install esptool
   ```

2. **Set up USB device access:**
   ```bash
   sudo usermod -a -G dialout $USER
   # Create udev rules if needed for specific devices
   ```

3. **Map USB devices:**
   - Use `lsusb` to identify device IDs
   - Create udev rules in `/etc/udev/rules.d/` if needed
   - Test with: `ls -l /dev/ttyUSB*`

4. **Run tests in cron or CI system:**
   ```bash
   # In your CI script
   cd /path/to/StarForgeOS/flash_tool
   ./test/run-headless-tests.sh
   ```

## Testing Scenarios

### 1. Basic Hardware Detection
Test that boards are detected correctly:
```bash
export DISPLAY=:99
Xvfb :99 -screen 0 1024x768x24 &
node test/headless-test-runner.js --list-ports
```

### 2. Chip Type Detection
Verify chip detection works:
```bash
node test/headless-test-runner.js --detect-chip --port /dev/ttyUSB0
```

### 3. Firmware Validation
Check that firmware files are valid:
```bash
node test/headless-test-runner.js \
  --port /dev/ttyUSB0 \
  --board esp32-c3 \
  --firmware ./firmware/
```

### 4. Full Integration Test
Run complete test suite:
```bash
./test/run-headless-tests.sh
```

## Troubleshooting

### "No serial ports found"
- Check USB connection: `lsusb`
- Check device permissions: `ls -l /dev/ttyUSB*`
- Add user to dialout group: `sudo usermod -a -G dialout $USER`
- Reload udev rules: `sudo udevadm control --reload-rules`

### "Xvfb failed to start"
- Check if display is already in use: `xdpyinfo -display :99`
- Kill existing Xvfb: `pkill Xvfb`
- Try a different display number: `export DISPLAY=:100`

### "Electron failed to start"
- Ensure DISPLAY is set: `echo $DISPLAY`
- Verify Xvfb is running: `xdpyinfo -display $DISPLAY`
- Check Node.js version: `node --version` (should be 18+)

### "esptool not found"
- Install: `pip install esptool`
- Or: `sudo apt-get install esptool`
- Verify in PATH: `which esptool.py`

### "Permission denied" on serial port
```bash
# Add user to dialout group
sudo usermod -a -G dialout $USER

# Log out and back in, or:
newgrp dialout

# Verify:
groups  # Should show 'dialout'
```

## Advanced Usage

### Multiple Boards

To test multiple boards, you can run tests in parallel:

```bash
# Terminal 1
export DISPLAY=:99
export TEST_PORT=/dev/ttyUSB0
Xvfb :99 -screen 0 1024x768x24 &
node test/flash-test-suite.js

# Terminal 2
export DISPLAY=:100
export TEST_PORT=/dev/ttyUSB1
Xvfb :100 -screen 0 1024x768x24 &
node test/flash-test-suite.js
```

### Custom Test Firmware

Place your test firmware in a directory:
```bash
mkdir -p test-firmware
cp path/to/firmware/*.bin test-firmware/
export TEST_FIRMWARE_DIR=./test-firmware
```

### Remote Testing

For remote servers, you can use SSH with X forwarding (though Xvfb is easier):
```bash
ssh -X user@server
export DISPLAY=:10.0
```

Or use Xvfb on the remote server as described above.

## Future Improvements

- [ ] Automated firmware flashing tests
- [ ] Multi-board parallel testing
- [ ] Performance benchmarks
- [ ] Integration with PlatformIO builds
- [ ] Automated regression testing
- [ ] Hardware-in-the-loop (HIL) testing

