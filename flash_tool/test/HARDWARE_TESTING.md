# Hardware Testing Guide for Flash Tool

## Overview

This guide explains how to test the StarForge Flash Tool on headless Linux servers with real ESP32 hardware connected via USB.

## Key Challenge: GUI Application on Headless Server

The flash tool is an **Electron GUI application**, which normally requires:
- A display
- Window manager
- User interaction

But we can run it on headless servers using:
1. **Xvfb (X Virtual Framebuffer)** - Creates a virtual display
2. **Headless test harness** - Tests core functionality without GUI
3. **CI/CD integration** - Automated testing pipelines

## Solution Architecture

### Option 1: Virtual Display (Xvfb) - Recommended

Xvfb creates a virtual X server that Electron can use:

```bash
# Start virtual display
Xvfb :99 -screen 0 1024x768x24 &

# Run Electron app (will use virtual display)
export DISPLAY=:99
electron .  # or npm start
```

**Pros:**
- Full Electron functionality
- Can actually run the GUI app
- Works with any Electron app

**Cons:**
- Requires Xvfb installation
- Slight overhead

### Option 2: Headless Test Harness

Extract and test core logic without Electron GUI:

```javascript
// Test the flash functions directly
const flashLogic = require('./flash-core'); // hypothetical extraction
await flashLogic.flashFirmware(port, board, firmwarePath);
```

**Pros:**
- Lightweight
- Fast
- No display needed

**Cons:**
- Requires refactoring code
- Doesn't test full GUI workflow

### Option 3: Hybrid Approach - What We've Implemented

Use Xvfb for full testing, but also provide headless test utilities:

1. **Full GUI tests** - Run actual Electron app with Xvfb
2. **Unit tests** - Test individual functions
3. **Integration tests** - Test with real hardware

## Setup Instructions

### 1. Install Xvfb

```bash
# Ubuntu/Debian
sudo apt-get update
sudo apt-get install xvfb

# Verify installation
Xvfb -help
```

### 2. Install Other Dependencies

```bash
# Node.js (if not already installed)
curl -fsSL https://deb.nodesource.com/setup_18.x | sudo -E bash -
sudo apt-get install -y nodejs

# esptool
pip3 install esptool
# or
sudo apt-get install esptool

# npm dependencies
cd flash_tool
npm install
```

### 3. Set Up USB Device Access

```bash
# Add user to dialout group (for serial port access)
sudo usermod -a -G dialout $USER

# Log out and back in, or:
newgrp dialout

# Verify permissions
ls -la /dev/ttyUSB* /dev/ttyACM*
```

### 4. Connect Hardware

- Connect ESP32 boards via USB
- Verify they appear: `lsusb`
- Check device nodes: `ls -l /dev/ttyUSB*`

## Running Tests

### Quick Start (Automated)

```bash
cd flash_tool
./test/run-headless-tests.sh
```

This script:
- Checks dependencies
- Starts Xvfb automatically
- Runs test suite
- Cleans up on exit

### Manual Testing

#### Step 1: Start Virtual Display

```bash
# Terminal 1: Start Xvfb
export DISPLAY=:99
Xvfb :99 -screen 0 1024x768x24 &
```

#### Step 2: Run Tests

```bash
# Terminal 2: Run test suite
export DISPLAY=:99
cd flash_tool
node test/flash-test-suite.js
```

#### Step 3: Test Individual Components

```bash
# List serial ports
node test/headless-test-runner.js --list-ports

# Detect chip type
node test/headless-test-runner.js --detect-chip --port /dev/ttyUSB0

# Validate firmware
node test/headless-test-runner.js \
  --port /dev/ttyUSB0 \
  --board esp32-c3 \
  --firmware ./firmware/
```

## Testing Scenarios

### Scenario 1: Hardware Detection

Test that your server can see connected ESP32 boards:

```bash
export DISPLAY=:99
Xvfb :99 -screen 0 1024x768x24 &
node test/headless-test-runner.js --list-ports
```

**Expected Output:**
```
[INFO] Found 2 serial port(s)
[SUCCESS] Found 1 potential ESP32 device(s):
  /dev/ttyUSB0 - Silicon Labs (VID:10c4)
```

### Scenario 2: Chip Detection

Verify that esptool can communicate with the device:

```bash
node test/headless-test-runner.js --detect-chip --port /dev/ttyUSB0
```

**Expected Output:**
```
[INFO] Detecting chip type on /dev/ttyUSB0...
[SUCCESS] Detected chip: esp32c3
```

### Scenario 3: Firmware Validation

Check that firmware files are valid before flashing:

```bash
node test/headless-test-runner.js \
  --port /dev/ttyUSB0 \
  --board esp32-c3 \
  --firmware ./path/to/firmware/ \
  --dry-run
```

### Scenario 4: Full Integration Test

Run complete test suite (requires firmware files):

```bash
export TEST_FIRMWARE_DIR=./firmware/
export TEST_PORT=/dev/ttyUSB0
./test/run-headless-tests.sh
```

## Homelab Server Setup

### Multi-Board Testing

If you have multiple ESP32 boards:

1. **Identify each board:**
   ```bash
   for port in /dev/ttyUSB*; do
     echo "Testing $port..."
     esptool.py --port $port flash_id
   done
   ```

2. **Run tests in parallel:**
   ```bash
   # Terminal 1: Board 1
   export DISPLAY=:99
   export TEST_PORT=/dev/ttyUSB0
   Xvfb :99 -screen 0 1024x768x24 &
   node test/flash-test-suite.js &
   
   # Terminal 2: Board 2
   export DISPLAY=:100
   export TEST_PORT=/dev/ttyUSB1
   Xvfb :100 -screen 0 1024x768x24 &
   node test/flash-test-suite.js &
   ```

### Automated Testing Script

Create a cron job or systemd service:

```bash
#!/bin/bash
# /usr/local/bin/test-starforge.sh

cd /path/to/StarForgeOS/flash_tool
export DISPLAY=:99

# Start Xvfb if not running
if ! xdpyinfo -display :99 &>/dev/null; then
    Xvfb :99 -screen 0 1024x768x24 &
    sleep 2
fi

# Run tests
./test/run-headless-tests.sh
```

### Docker Container (Optional)

For isolated testing environments:

```dockerfile
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    xvfb \
    nodejs \
    npm \
    python3 \
    python3-pip \
    udev

RUN pip3 install esptool

WORKDIR /app
COPY flash_tool/ ./

RUN npm install

# Run with USB device access:
# docker run --device=/dev/ttyUSB0 -e DISPLAY=:99 ...
```

## Running on a machine with a board

These checks need a serial port and an ESP32. They are not run in GitHub Actions. The Mac and Windows app builds run in `.github/workflows/build-flasher.yml`.

### GitLab CI

```yaml
test-flash-tool:
  tags:
    - hardware  # Runner tag for hardware-enabled runners
  
  before_script:
    - export DISPLAY=:99
    - Xvfb :99 -screen 0 1024x768x24 &
    - sleep 2
  
  script:
    - cd flash_tool
    - npm install
    - ./test/run-headless-tests.sh
```

## Troubleshooting

### Xvfb Issues

**Problem:** `Xvfb: command not found`
```bash
sudo apt-get install xvfb
```

**Problem:** Display already in use
```bash
# Kill existing Xvfb
pkill Xvfb

# Or use different display
export DISPLAY=:100
Xvfb :100 -screen 0 1024x768x24 &
```

### Serial Port Issues

**Problem:** Permission denied
```bash
# Add user to dialout group
sudo usermod -a -G dialout $USER
newgrp dialout

# Or fix permissions temporarily
sudo chmod 666 /dev/ttyUSB0
```

**Problem:** Device not found
```bash
# Check USB devices
lsusb

# Check device nodes
ls -la /dev/ttyUSB* /dev/ttyACM*

# Check udev rules
sudo udevadm control --reload-rules
```

### Electron Issues

**Problem:** Electron fails to start
```bash
# Verify DISPLAY is set
echo $DISPLAY

# Test X server
xdpyinfo -display $DISPLAY

# Check Node.js version (needs 18+)
node --version
```

## Best Practices

1. **Always use Xvfb** - Don't rely on existing X servers
2. **Clean up** - Kill Xvfb processes after tests
3. **Use timeouts** - Prevent tests from hanging
4. **Log everything** - Debugging headless is harder
5. **Test incrementally** - Start with hardware detection, then chip detection, then flashing
6. **Backup firmware** - Always test with known-good firmware first
7. **Monitor resources** - Xvfb uses memory; clean up properly

## Next Steps

- [ ] Set up automated nightly builds
- [ ] Create test firmware repository
- [ ] Implement hardware matrix testing
- [ ] Add performance benchmarks
- [ ] Create failure notification system

