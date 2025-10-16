# Tracer - ESP32-C3 Timing System

**The ultimate dual-mode FPV race timer** - Works standalone with WiFi or integrates seamlessly with RotorHazard server. Built on ESP32-C3 for maximum performance and reliability.

## 🏁 Two Modes, One Device

### 🌐 Standalone Mode - WiFi Racing
**Perfect for casual solo racing and practice laps**
- **Instant setup** - Just power on and connect to WiFi
- **Beautiful web interface** - Works on any device (phone, tablet, laptop)
- **Real-time RSSI monitoring** - Visual feedback with live graphs
- **Audio announcements** - Lap times announced via browser speech synthesis
- **Multi-band support** - Raceband, Fatshark, Boscam A/E frequencies
- **No server required** - Everything runs on the ESP32-C3


### 🔌 RotorHazard Node Mode - Professional Racing
**Full integration with RotorHazard server**
- **100% compatible** - Drop-in replacement for standard RotorHazard solo nodes
- **High-precision timing** - FreeRTOS-based real-time processing
- **USB connectivity** - Plug and play with existing RotorHazard setups
- **Optimized performance** - WiFi disabled for maximum timing accuracy

### 🎯 **Dual-Mode Operation**
Switch between modes instantly - no firmware changes needed. Use a simple jumper or switch to select WiFi or RotorHazard mode.

## 🎮 Perfect For

- **FPV racing clubs** - Standalone timing for practice sessions
- **RotorHazard users** - Professional timing node for competitions
- **DIY enthusiasts** - Easy to build and customize
- **Mobile racing** - Take timing anywhere with WiFi mode
- **Testing and development** - Debug FPV setups and frequencies

## 🛠️ Quick Start

### Standalone Mode (WiFi)
1. Power on the device
2. Connect to WiFi network "tracer-XXXX"
3. Open browser to http://tracer.local
4. Start racing!

### RotorHazard Node Mode
1. Connect via USB to your RotorHazard server
2. Add as timing node in RotorHazard
3. Full professional features available!

## 📋 Hardware Requirements

- ESP32-C3 SuperMini development board
- RX5808 FPV receiver module  
- Mode selection switch (optional)
- Basic wiring tools

## 🔧 Technical Details

For detailed technical information, pin configurations, and setup instructions, see the `docs/` folder:

- **[Setup Guide](docs/setup.md)** - Complete installation and configuration
- **[Hardware Guide](docs/hardware.md)** - PCB design and wiring diagrams
- **[Display Customization](docs/display_customization.md)** - UI customization options

## 🏗️ Building

```bash
cd Tracer/
pio run -e esp32-c3-supermini --target upload
pio run -e esp32-c3-supermini --target uploadfs
```

**Note**: Both commands are required - one uploads the firmware, the other uploads the web interface files.

## 🐛 Troubleshooting

Common issues and solutions are covered in the [Setup Guide](docs/setup.md). For RSSI problems, frequency issues, or connectivity troubleshooting, check the documentation.

---

**Ready to race?** Get started with the [Setup Guide](docs/setup.md) and join the FPV racing community!


