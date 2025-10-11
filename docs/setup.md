# RotorHazard Lite - Setup Guide

This guide walks you through setting up the development environment and flashing the RotorHazard Lite firmware to an ESP32-C3 SuperMini.

## Prerequisites

- macOS (this guide is written for Mac)
- ESP32-C3 SuperMini development board
- USB cable
- Basic terminal knowledge

## Step 1: Install PlatformIO

PlatformIO is the build system used for this project. Install it using pip:

```bash
# Install PlatformIO Core
pip3 install platformio

# Verify installation
pio --version
```

**Expected output**: `PlatformIO Core, version 6.1.18` (or similar)

## Step 2: Clone and Navigate to Project

```bash
# Navigate to the project directory
cd /path/to/RotorHazard/src/lite/

# Verify you're in the right place (should see platformio.ini)
ls -la
```

## Step 3: Connect ESP32-C3 SuperMini

1. Connect your ESP32-C3 SuperMini to your Mac via USB
2. Verify the device is detected:

```bash
ls /dev/cu.*
```

**Expected output**: You should see something like `/dev/cu.usbmodemXXXX` where XXXX is a number.

## Step 4: Build the Firmware

Build the firmware for ESP32-C3 SuperMini:

```bash
pio run -e esp32-c3-supermini
```

**Expected output**: 
- Compilation messages
- `RAM: [=         ] 12.3% (used 40460 bytes from 327680 bytes)`
- `Flash: [======    ] 62.3% (used 816974 bytes from 1310720 bytes)`
- `========================= [SUCCESS] Took X.XX seconds =========================`

## Step 5: Upload Firmware

Upload the firmware to your ESP32-C3 (replace `/dev/cu.usbmodemXXXX` with your actual device):

```bash
pio run -e esp32-c3-supermini --target upload --upload-port /dev/cu.usbmodemXXXX
```

**Expected output**:
- `Connecting...`
- `Chip is ESP32-C3 (QFN32) (revision v0.4)`
- `Features: WiFi, BLE, Embedded Flash 4MB (XMC)`
- `Uploading stub...`
- `Writing at 0x00010000... (100 %)`
- `========================= [SUCCESS] Took X.XX seconds =========================`

## Step 6: Monitor Serial Output

To see the firmware running and debug output, use the pio monitor command:

```bash
# Connect to your ESP32-C3 (replace XXXX with your device number)
pio device monitor --port /dev/cu.usbmodemXXXX --baud 115200 --environment esp32-c3-supermini
```

**To exit**: Press `Ctrl+C`

**Expected output**:
```
=== RotorHazard Lite ESP32-C3 Timer ===
Version: 1.0.0
Single-core RISC-V processor
Initializing timing core...
TimingCore: Initializing...
Setting up RX5808...
TimingCore: Ready
Initializing mode: STANDALONE/WIFI
=== WIFI/LITE MODE ACTIVE ===
Connect to WiFi: rotorhazard-XXXX
Web interface: http://lite.local
ESP32-C3 Single-core operation
Setup complete!
```

### Alternative Serial Monitor Options

If you prefer a GUI application:

```bash
# Install Serial app (modern GUI)
brew install --cask serial

# Install CoolTerm (cross-platform)
brew install --cask coolterm
```

## Troubleshooting

### Port Busy Error
If you get "port is busy" error:
```bash
# Check what's using the port
lsof /dev/cu.usbmodemXXXX

# Kill the process (replace PID with actual number)
kill PID

# Try upload again
pio run -e esp32-c3-supermini --target upload --upload-port /dev/cu.usbmodemXXXX
```

### Device Not Found
If you don't see `/dev/cu.usbmodemXXXX`:
1. Check USB cable connection
2. Try a different USB port
3. Unplug and reconnect the device
4. Check if device drivers are installed

### Build Errors
If build fails:
1. Make sure you're in the correct directory (`RotorHazard/src/lite/`)
2. Check that `platformio.ini` exists
3. Try cleaning and rebuilding:
   ```bash
   pio run -e esp32-c3-supermini --target clean
   pio run -e esp32-c3-supermini
   ```

## Quick Reference

| Command | Purpose |
|---------|---------|
| `pio run -e esp32-c3-supermini` | Build firmware |
| `pio run -e esp32-c3-supermini --target upload` | Upload firmware |
| `pio run -e esp32-c3-supermini --target clean` | Clean build |
| `ls /dev/cu.*` | List USB devices |
| `screen /dev/cu.usbmodemXXXX 115200` | Monitor serial output |

## Next Steps

After successful upload:
1. **WiFi Mode**: Set mode switch to GND, connect to "rotorhazard-XXXX" WiFi, open http://lite.local
2. **Node Mode**: Set mode switch to 3.3V (or leave floating), connect to RotorHazard server via USB
3. **Hardware Setup**: Connect RX5808 module using Hertz-hunter compatible pinout (see `hardware.md`)

## Hardware Requirements

- ESP32-C3 SuperMini development board (~$1.50 wholesale)
- RX5808 FPV receiver module
- Mode selection switch (GND=Node, 3.3V=WiFi)
- Basic wiring supplies

See `hardware.md` for detailed pin connections and PCB design considerations.
