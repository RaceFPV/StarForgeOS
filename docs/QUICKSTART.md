# StarForge OS Quick Start Guide

Welcome to StarForge OS! This guide will help you get up and running quickly.

---

## 📱 Using the Flash Tool

The StarForge Flash Tool makes it easy to flash firmware to your ESP32 board.

### Download & Install
1. Download the latest Flash Tool installer for your OS from [Releases](https://github.com/RaceFPV/StarForgeOS/releases)
2. Install and run the application
3. The tool will automatically detect your ESP32 when connected

### Flashing Firmware
1. Connect your ESP32 board via USB
2. Select your board type from the dropdown
3. Click "Flash Latest Firmware"
4. Wait for the process to complete (~30 seconds)

**The Flash Tool automatically updates everything:**
- ✅ Bootloader
- ✅ Firmware
- ✅ Web Interface (SPIFFS filesystem)
- ✅ Partitions

No need to manually upload the filesystem - it's all included!

---

## ⚠️ IMPORTANT: Mode Switch Configuration

**After flashing, your board boots into RotorHazard mode by default!**

To access the **WiFi Web Interface** (Standalone Mode), you MUST configure the mode switch:

### Option 1: Physical Mode Switch (Recommended)
Connect a physical switch between the MODE_SWITCH_PIN and GND:
- **Switch CLOSED (connected to GND)** = WiFi/Standalone Mode (Web Interface)
- **Switch OPEN (floating/pullup)** = RotorHazard Node Mode (default)

### Option 2: Hardwire to GND
If you only want WiFi mode, permanently connect MODE_SWITCH_PIN to GND with a wire or jumper.

### Option 3: LCD Touch Button (JC2432W328C only)
If you have the JC2432W328C board with LCD, you can switch modes using the touchscreen button instead of a physical switch.

**Without a mode switch or jumper to GND, you won't see the WiFi access point or web interface!**

---

## 📋 Hardware Pin Configurations

### ESP32-C3 SuperMini (Hertz-Hunter Compatible)

**RX5808 Connections:**
| Function | GPIO | Notes |
|----------|------|-------|
| RSSI Input | GPIO 3 | ADC1_CH3 - Analog RSSI from RX5808 |
| DATA (MOSI) | GPIO 6 | SPI Data to RX5808 |
| CLK (SCK) | GPIO 4 | SPI Clock to RX5808 |
| SEL (CS/LE) | GPIO 7 | Latch Enable to RX5808 |

**Control:**
| Function | GPIO | Notes |
|----------|------|-------|
| Mode Switch | GPIO 1 | Connect to GND for WiFi mode |

---

### ESP32 Dev Module (Generic ESP32-WROOM)

**RX5808 Connections:**
| Function | GPIO | Notes |
|----------|------|-------|
| RSSI Input | GPIO 34 | ADC1_CH6 - Analog RSSI from RX5808 |
| DATA (MOSI) | GPIO 23 | SPI Data to RX5808 |
| CLK (SCK) | GPIO 18 | SPI Clock to RX5808 |
| SEL (CS/LE) | GPIO 5 | Latch Enable to RX5808 |

**Control:**
| Function | GPIO | Notes |
|----------|------|-------|
| Mode Switch | GPIO 33 | Connect to GND for WiFi mode |

---

### JC2432W328C (ESP32 with ST7789 LCD & Touch)

**RX5808 Connections:**
| Function | GPIO | Notes |
|----------|------|-------|
| RSSI Input | GPIO 35 | ADC1_CH7 - Analog RSSI from RX5808 |
| DATA (MOSI) | GPIO 21 | SPI Data to RX5808 |
| CLK (SCK) | GPIO 16 | SPI Clock to RX5808 |
| SEL (CS/LE) | GPIO 17 | Latch Enable to RX5808 |

**Control:**
| Function | GPIO | Notes |
|----------|------|-------|
| Mode Switch | GPIO 22 | Connect to GND for WiFi mode (or use LCD button) |
| Power Button | GPIO 22 | Momentary button to GND (long press = sleep) |

**Battery Monitoring (Optional):**
| Function | GPIO | Notes |
|----------|------|-------|
| Battery Sense | GPIO 34 | 2:1 voltage divider (100kΩ + 100kΩ) |

**Circuit:** Battery+ → 100kΩ → GPIO34 → 100kΩ → GND

---

## 🚀 First-Time Setup

### 1. Wire Your Hardware
- Connect your RX5808 module using the pin configuration for your board (see above)
- **IMPORTANT:** Connect MODE_SWITCH_PIN to GND (or install a physical switch)
- Connect VTX video transmitter to your quad

### 2. Power On
- The board will boot in the mode determined by your MODE_SWITCH_PIN
- **GND = WiFi Mode** (you'll see "SFOS-XXXXXX" WiFi network appear)
- **Floating = RotorHazard Mode** (no WiFi network, ready for RotorHazard connection)

### 3. Connect to WiFi (Standalone Mode)
1. Look for WiFi network: `SFOS-XXXXXX` (where XXXXXX is part of your board's MAC address)
2. Connect to it (open network, no password)
3. Open browser to: `http://192.168.4.1` or `http://sfos.local`

### 4. Configure Your Frequency
1. Select your VTX band (Raceband, Fatshark, Boscam A/E)
2. Select your channel (1-8)
3. Set your RSSI threshold (adjust while hovering near the gate)

### 5. Start Racing!
1. Click "▶ Start Race" - you'll see a 5-4-3-2-1-GO countdown
2. Fly through the gate to record laps
3. View lap times in real-time on the web interface

---

## 🎮 Web Interface Features

- **Real-time RSSI Graph** - See signal strength as you fly
- **Live Lap Timing** - Automatic lap detection and recording
- **Adjustable Threshold** - Fine-tune detection sensitivity
- **Best/Last Lap Display** - Track your performance
- **Multiple Bands** - Support for all standard FPV bands
- **Race Start Countdown** - 5-second countdown with audio beeps

---

## 🔧 Troubleshooting

### "I don't see the WiFi network"
- Check that MODE_SWITCH_PIN is connected to GND
- Verify power to the board (LED should be lit)
- Try factory reset (hold power button for 3s on LCD boards)

### "RSSI reading is 0 or 255"
- Check RX5808 connections (DATA, CLK, SEL pins)
- Verify RSSI_INPUT_PIN is connected to RX5808 RSSI output
- Make sure VTX is powered on and transmitting

### "Laps aren't being detected"
- Lower the RSSI threshold in the web interface
- Make sure you fly close enough to the gate
- Check that your VTX frequency matches the receiver frequency

### "False laps when starting race"
- This is normal if you start on the gate - the system has a 3-second grace period
- The 5-second countdown gives you time to take off
- Hover away from the gate before the "GO!" beep

### "Web interface looks broken"
- Clear your browser cache (Ctrl+F5 or Cmd+Shift+R)
- Try a different browser
- Reflash firmware with Flash Tool to update web files

---

## 📚 Advanced Features

### Battery Monitoring (JC2432W328C only)
Install a 2:1 voltage divider on GPIO34 to monitor your 1S LiPo battery:
- Battery voltage displayed on LCD
- Percentage indicator
- Works with WiFi active (no ADC conflicts)

### Power Button (JC2432W328C only)
- **Short press:** Wake from sleep
- **Long press (3s):** Enter deep sleep mode
- Saves battery when not racing

### RotorHazard Node Mode
Connect MODE_SWITCH_PIN to 3.3V (or leave floating) to use as a RotorHazard node:
- Compatible with RotorHazard race timing system
- Automatic frequency synchronization
- Multi-node support for multiple gates

---

## 📞 Need Help?

- **GitHub Issues:** [Report bugs or request features](https://github.com/RaceFPV/StarForgeOS/issues)
- **Documentation:** Check the `docs/` folder for detailed technical info
- **Hardware Guide:** See `docs/hardware.md` for detailed wiring diagrams

---

## 🎯 Quick Reference Card

```
┌─────────────────────────────────────────────┐
│  STARFORGE OS - QUICK REFERENCE            │
├─────────────────────────────────────────────┤
│  MODE SWITCH:                               │
│  • GND = WiFi/Web Interface                 │
│  • Floating/3.3V = RotorHazard Node         │
│                                             │
│  WEB INTERFACE:                             │
│  • WiFi: SFOS-XXXXXX (open network)         │
│  • URL: http://192.168.4.1                  │
│  • URL: http://sfos.local                   │
│                                             │
│  RACE START:                                │
│  • 5-second countdown with beeps            │
│  • 3-second grace period (no false laps)    │
│                                             │
│  DEFAULT SETTINGS:                          │
│  • Frequency: 5800 MHz (Raceband R5)        │
│  • Threshold: 100 RSSI                      │
│  • Min Lap Time: 3 seconds                  │
└─────────────────────────────────────────────┘
```

Happy Racing! 🏁

