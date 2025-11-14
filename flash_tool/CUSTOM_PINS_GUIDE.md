# Custom Pin Configuration Guide

## Overview

The StarForge Flash Tool now supports **custom pin configurations** for users with non-standard boards. This allows you to use prebuilt firmware binaries while defining your own pin mappings, without needing to recompile the firmware.

## How It Works

The system uses a `config.json` file stored on the ESP32's SPIFFS filesystem. On boot, the firmware checks for this file and uses the custom pins if found. Otherwise, it falls back to the defaults in `config.h`.

### Architecture

```
┌──────────────────────────────────────┐
│ Flash Tool (Your Computer)            │
│  ├─ User enters custom pins in UI    │
│  ├─ Generates config.json             │
│  ├─ Creates SPIFFS partition image   │
│  └─ Flashes to ESP32                  │
└──────────────────────────────────────┘
                  ↓
┌──────────────────────────────────────┐
│ ESP32 Firmware Boot                   │
│  1. Check SPIFFS for /config.json    │
│  2. If found & enabled → Use custom   │
│  3. If not found → Use config.h       │
└──────────────────────────────────────┘
```

## Using the Flash Tool

### Step 1: Select Your Board & Port

1. Open the StarForge Flash Tool
2. Select your board type (closest match to your hardware)
3. Select the serial port
4. Choose firmware source (Latest Release or Local Folder)

### Step 2: Configure Custom Pins (Optional)

1. Click **"⚙️ Advanced Configuration"** to expand the section
2. Check **"Enable Custom Pin Configuration"**
3. Enter your custom GPIO pin numbers:

#### Core Pins (Required)
- **RSSI Input (ADC)**: Analog pin reading RSSI voltage from RX5808
- **RX5808 Data (MOSI)**: SPI data line to RX5808
- **RX5808 Clock (SCK)**: SPI clock line to RX5808
- **RX5808 Select (CS)**: SPI chip select for RX5808
- **Mode Switch**: Pin to switch between standalone and RotorHazard modes

#### Optional Pins (LCD Boards Only)
- **Power Button**: Long press to enter deep sleep
- **Battery ADC**: Analog pin for battery voltage monitoring
- **Audio DAC**: DAC pin for audio output (beeps)

### Step 3: Use Pin Presets (Optional)

The tool includes presets for common boards:
- **ESP32-C3 SuperMini (Default)**
- **ESP32-C6 (Default)**
- **ESP32 Generic (Default)**

Select a preset to auto-fill pin values, then modify as needed.

### Step 4: Flash

Click **"⚡ Flash Firmware"**

The tool will:
1. Download or load the firmware
2. Generate a SPIFFS partition with your custom config
3. Flash everything to the ESP32

### Step 5: Verify

After flashing, open the Serial Monitor (115200 baud). You should see:

```
ConfigLoader: Custom pin configuration loaded successfully!
  RSSI Input: GPIO3
  RX5808 Data: GPIO6
  RX5808 CLK: GPIO4
  RX5808 SEL: GPIO7
  Mode Switch: GPIO1
```

## Reverting to Defaults

To go back to the default pins from `config.h`:

### Option 1: Disable Custom Config
1. Uncheck "Enable Custom Pin Configuration"
2. Flash again

This writes a config.json with `enabled: false`, telling the firmware to use defaults.

### Option 2: Erase Flash
1. Click "🗑️ Erase Flash" in the tool
2. Flash firmware normally (without custom config)

This completely wipes the SPIFFS, so no config.json exists.

## Manual Configuration (Advanced)

If you prefer to create `config.json` manually:

### Example config.json

```json
{
  "custom_pins": {
    "enabled": true,
    "rssi_input": 3,
    "rx5808_data": 6,
    "rx5808_clk": 4,
    "rx5808_sel": 7,
    "mode_switch": 1,
    "power_button": 0,
    "battery_adc": 0,
    "audio_dac": 0
  }
}
```

### Upload via PlatformIO

1. Create `StarForgeOS/data/config.json` with your custom config
2. Run: `pio run --target uploadfs`

This uploads the SPIFFS filesystem to the ESP32.

### Upload via Python Script

The flash tool includes a standalone Python script:

```bash
cd StarForgeOS/flash_tool/resources/scripts
python3 generate_spiffs.py config.json output.bin

# Flash manually
esptool.py --chip esp32c3 --port /dev/ttyUSB0 write_flash 0x290000 output.bin
```

## Pin Requirements

### ADC Pins (RSSI & Battery)
- **Must be ADC1 pins** (ADC2 conflicts with WiFi)
- ESP32-C3: GPIO0-4 (ADC1_CH0-4)
- ESP32-C6: GPIO0-6 (ADC1_CH0-6)
- ESP32: GPIO32-39 (ADC1_CH4-7)
- ESP32-S3: GPIO1-10 (ADC1_CH0-9)

### SPI Pins (RX5808)
- Can use any GPIO with output capability
- Avoid strapping pins if possible (GPIO0, GPIO2, GPIO15, etc.)

### Important Notes
- **GPIO3** on ESP32-C6 is used for USB (avoid if using USB serial)
- **GPIO pins > 40** are only available on some boards
- Test one pin at a time if you're unsure about your board's layout

## Troubleshooting

### Firmware won't boot after custom pins
- **Symptom**: Board doesn't respond, no serial output
- **Fix**: Erase flash and reflash without custom config

### Custom config not loading
- **Symptom**: Serial says "Using default pin configuration"
- **Possible causes**:
  - `enabled: false` in config.json
  - SPIFFS not flashed correctly
  - JSON parsing error (check formatting)
- **Fix**: Check serial output for ConfigLoader messages

### ADC not working
- **Symptom**: RSSI always 0 or 255
- **Fix**: Verify pin is ADC1 (not ADC2), check wiring

### Python/mkspiffs not found
- **Symptom**: "SPIFFS generation failed"
- **Fix**: Install Python and PlatformIO (which includes mkspiffs)

```bash
# Install PlatformIO
pip install platformio

# This also installs mkspiffs
```

## Safety Warnings

⚠️ **Important**:
- Double-check pin numbers before flashing
- Incorrect pins can cause shorts or damage
- Some pins have special functions (boot mode, flash, etc.)
- Test with LEDs or multimeter before connecting sensitive hardware

## Support

If you encounter issues:
1. Check the serial output for error messages
2. Verify your pin numbers against your board's documentation
3. Try reverting to default config
4. Open an issue on GitHub with:
   - Board type
   - Custom pin configuration
   - Serial output logs

## Technical Details

### SPIFFS Partition
- **Location**: 0x290000 (default)
- **Size**: ~1.5MB
- **Files**: `/config.json`, web UI files (if included)

### Config Loader Implementation
- **Source**: `StarForgeOS/src/config_loader.cpp`
- **Loads at boot**: Before any pins are initialized
- **Fallback**: Always safe to use config.h defaults

### Flash Tool Integration
- **Generator**: `resources/scripts/generate_spiffs.py`
- **Backend**: `main.js` generateCustomSPIFFS()
- **Frontend**: `renderer.js` getCustomPinConfig()

---

**Happy flashing!** 🚀

