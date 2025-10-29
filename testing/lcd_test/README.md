# JC2432W328C LCD Test Project

Test project for the JC2432W328C development board featuring an ESP32-D0WD-V3 with integrated touch display and battery charging.

## Board Specifications

### Chip Information
- **Chip**: ESP32-D0WD-V3 (revision v3.1)
- **Cores**: Dual Core (Core 0 + Core 1) + LP Core
- **CPU Frequency**: 240MHz
- **Crystal**: 40MHz
- **Features**: Wi-Fi, Bluetooth, Vref calibration in eFuse

### Board Features
- Touch display (likely 2.8" or 3.2")
- Battery charging circuit
- USB-to-serial converter
- Various GPIO exposed

## Quick Start

### Prerequisites
- PlatformIO installed (`pip install platformio`)
- USB cable
- JC2432W328C board

### Build and Upload

```bash
# Navigate to project directory
cd lcd_test

# Build firmware
pio run

# Upload to board (adjust port if needed)
pio run --target upload --upload-port /dev/ttyUSB0

# Monitor serial output
pio device monitor --port /dev/ttyUSB0 --baud 115200
```

### Expected Serial Output
```
=================================
JC2432W328C Board Test
=================================

Chip Information:
  Chip Model: ESP32-D0WD-V3
  Chip Revision: 3
  CPU Frequency: 240 MHz
  Flash Size: 4194304 bytes
  Free Heap: 295536 bytes
  SDK Version: v4.4.x

Features:
  ✓ Dual Core (Core 0 + Core 1)
  ✓ Wi-Fi
  ✓ Bluetooth
  ✓ Touch Display
  ✓ Battery Charging

Setup complete!
=================================

[2000 ms] Heartbeat - Free Heap: 295536 bytes
[4000 ms] Heartbeat - Free Heap: 295536 bytes
```

## Pin Configuration

**Note**: The exact pin configuration for the display and touch controller needs to be determined. Common configurations:

### Possible Display Pins (ST7789/ILI9341)
```
TFT_MISO  = 19  (or 12)
TFT_MOSI  = 23  (or 13)
TFT_SCLK  = 18  (or 14)
TFT_CS    = 15  (or 5)
TFT_DC    = 2   (or 4)
TFT_RST   = 4   (or -1)
TFT_BL    = ?   (backlight control)
```

### Possible Touch Pins
```
TOUCH_CS  = ?
TOUCH_IRQ = ?
```

### Battery/Charging Pins
```
VBAT_ADC  = ?   (battery voltage sense)
CHG_STAT  = ?   (charging status)
```

## Next Steps

1. **Identify Display Controller**
   - Check board documentation or use multimeter to trace pins
   - Common controllers: ST7789, ILI9341, ST7735

2. **Test Display**
   - Configure pins in platformio.ini
   - Add TFT_eSPI initialization
   - Test with simple graphics

3. **Test Touch**
   - Identify touch controller (resistive vs capacitive)
   - Configure touch pins
   - Test touch input

4. **Test Battery Charging**
   - Measure battery voltage
   - Check charging status indicator
   - Test USB power vs battery power

## Troubleshooting

### Board Not Detected
```bash
# List USB devices
ls /dev/ttyUSB*
ls /dev/ttyACM*

# Check dmesg for connection info
dmesg | tail -20
```

### Upload Fails
- Hold BOOT button while uploading
- Try lower baud rate: `upload_speed = 115200`
- Check USB cable (some are charge-only)

### No Serial Output
- Check baud rate matches (115200)
- Try pressing EN/RESET button
- Check USB-to-serial chip driver

## Resources

- [ESP32 Technical Reference](https://www.espressif.com/sites/default/files/documentation/esp32_technical_reference_manual_en.pdf)
- [TFT_eSPI Library](https://github.com/Bodmer/TFT_eSPI)
- [ESP32 Arduino Core](https://github.com/espressif/arduino-esp32)

## License

This test project is part of the StarForge project.

