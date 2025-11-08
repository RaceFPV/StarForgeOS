# StarForge - ESP32 Drone Race Timing System

**The ultimate dual-mode FPV race timer** - Works standalone with WiFi or integrates seamlessly with RotorHazard server. Built on ESP32 for maximum performance and reliability.

> **📚 [Quick Start Guide →](docs/QUICKSTART.md)** - New user? Start here for step-by-step setup instructions, pinouts, and troubleshooting!

Want to contribute, learn more, or need help, reach out to our Rotorhazard discord channel [here](https://discord.gg/Ep8sqJmh9d)

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

**Want a fully assembled version?** Pre-built boards are available at [racefpv.io](https://racefpv.io)

> **Building your own?** Check out the [DIY Hardware Guide](docs/hardware.md) for the RX5808-based version with complete wiring diagrams and PCB design tips.
>

### Standalone Mode (WiFi)
1. Power on the device
2. Connect to WiFi network "SFOS-XXXX"
3. Open browser to http://sfos.local
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

## 💾 Flashing Firmware

### 🚀 Easy Way - StarForge Flash Tool (Recommended)

**No command line required!** Use our cross-platform desktop app:

1. Download the latest release from [GitHub Releases](https://github.com/RaceFPV/StarForgeOS/releases/latest)
   - **macOS**: `StarForge-Flash-Tool-X.X.X.dmg`
   - **Windows**: `StarForge-Flash-Tool-Setup-X.X.X.exe`
2. Connect your ESP32 board via USB
3. Select your board type and click "Flash Firmware"
4. Done! The tool automatically downloads and flashes the latest firmware

**Features**: Auto-detection, one-click flashing, progress tracking, supports all board types.

📖 Full documentation: [`flash_tool/README.md`](flash_tool/README.md)

### ⚙️ Manual Way - PlatformIO (For Developers)

```bash
cd StarForgeOS/
pio run -e esp32-c3-supermini --target upload
pio run -e esp32-c3-supermini --target uploadfs
```

**Note**: Both commands are required - one uploads the firmware, the other uploads the web interface files.

Or use the Makefile for easier commands:

```bash
make                                    # Show all available commands
make build BOARD=esp32-c3-supermini    # Build firmware
make upload BOARD=esp32-c3-supermini   # Build and upload
make monitor BOARD=esp32-c3-supermini  # Open serial monitor
```

## 🧪 Testing (For Developers)

StarForgeOS includes comprehensive tests to ensure compatibility across all supported ESP32 board types.

### Quick Validation (No Hardware Required)
```bash
make test  # Validates all board configs compile correctly (~4-5 minutes)
```

This runs:
- ✅ Hardware configuration tests (pin definitions, constants)
- ✅ WiFi functionality tests
- ✅ Protocol unit tests (RotorHazard compatibility)

### Hardware Specific Testing
```bash
# Test specific board with connected hardware
make test-board BOARD=test-esp32-c3

# Test RotorHazard protocol integration
make test-rotorhazard PORT=/dev/ttyUSB0 BOARD=esp32dev
```

### Continuous Integration
Every pull request automatically runs build validation across all board types via GitHub Actions, ensuring no breaking changes.

**📖 Full testing documentation**: See [`test/README.md`](test/README.md) for complete details on the test suite, CI/CD strategy, and contribution guidelines.

