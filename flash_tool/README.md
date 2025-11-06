# StarForge Flash Tool

A cross-platform desktop application for flashing StarForge firmware to ESP32 devices.

## Features

- ✅ **Cross-Platform** - Works on macOS, Windows, and Linux
- 🔌 **Auto-Detection** - Automatically detects connected ESP32 boards
- 📦 **GitHub Integration** - Downloads latest firmware releases automatically
- 📁 **Local Files** - Flash from local firmware folders
- 📊 **Real-time Progress** - Live progress bars and console output
- 🛡️ **Safe** - Validates board types and prevents common flashing errors

## Installation

### For Users

**The easy way - no technical knowledge required!**

1. Go to [Releases](https://github.com/RaceFPV/StarForgeOS/releases/latest)
2. Download for your platform:
   - **macOS**: `StarForge-Flash-Tool-X.X.X.dmg`
   - **Windows**: `StarForge-Flash-Tool-Setup-X.X.X.exe`
3. Install and run - that's it!

No Python, no command line, no dependencies. Just download and flash!

### For Developers

```bash
cd flash_tool

# Install dependencies
npm install

# Run in development mode
npm start

# Build for your platform
npm run build

# Build for all platforms (requires macOS for Mac builds)
npm run build:all
```

## Requirements

### Runtime Requirements

- **esptool.py** must be installed and available in PATH
  - macOS/Linux: `pip install esptool`
  - Windows: `pip install esptool` or download standalone executable

### Development Requirements

- Node.js 18+ 
- npm or yarn
- Python 3.7+ (for esptool)

## Usage

1. **Connect your ESP32 board** via USB
2. **Select your board type** from the dropdown
3. **Select the serial port** (auto-detected)
4. **Choose firmware source**:
   - **Latest Release**: Downloads from GitHub automatically
   - **Local Folder**: Use locally built firmware
5. **Click "Flash Firmware"**
6. Wait for completion (don't disconnect during flash!)

## Supported Boards

- ESP32-C3 SuperMini
- ESP32-C3 Dev Module
- ESP32-C6 Dev Module (WiFi 6)
- ESP32 Dev Module (Original ESP32)
- ESP32-S3 Dev Module
- ESP32-S3 Touch LCD (no PSRAM)
- ESP32-S2 Dev Module
- JC2432W328C (ESP32 with LCD/Touch)

## Building

### Build for macOS

```bash
npm run build:mac
```

Output: `dist/StarForge Flasher-1.0.0.dmg`

### Build for Windows

```bash
npm run build:win
```

Output: `dist/StarForge Flasher Setup 1.0.0.exe`

### Build for Linux

```bash
npm run build:linux
```

Output: `dist/StarForge Flasher-1.0.0.AppImage`

## Troubleshooting

### "No ports found"

- Ensure your ESP32 is connected via USB
- Install USB-to-Serial drivers (CP210x or CH340)
- Click "Refresh Ports" after connecting

### "esptool not found"

Install esptool:
```bash
pip install esptool
```

Or on Windows, download the standalone executable and add to PATH.

### "Permission denied" (Linux/macOS)

Add your user to the dialout group:
```bash
sudo usermod -a -G dialout $USER
sudo usermod -a -G uucp $USER
```

Then log out and back in.

### Flash fails at specific percentage

- Try a lower baud rate (460800 or 115200)
- Use a better quality USB cable
- Try a different USB port

## GitHub Integration

The app fetches releases from:
```
https://github.com/YOUR_USERNAME/StarForge/releases/latest
```

Make sure to update `main.js` with your actual GitHub username/repo.

## License

MIT License - See LICENSE file for details

## Support

For issues and support, visit: https://discord.gg/Ep8sqJmh9d

