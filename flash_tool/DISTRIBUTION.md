# StarForge Flasher - Distribution Guide

## 🚀 Quick Start for Users

### Download Pre-built Installers

Go to the [latest release](https://github.com/RaceFPV/StarForgeOS/releases/latest) and download:

- **macOS**: `StarForge-Flasher-1.0.0.dmg` (~35-40MB)
- **Windows**: `StarForge-Flasher-Setup-1.0.0.exe` (~40-50MB)

### Installation

**macOS:**
1. Download the `.dmg` file
2. Open it and drag "StarForge Flasher" to Applications
3. **If you see "app is damaged" error:**
   - Open Terminal and run: `xattr -cr /Applications/StarForge\ Flash\ Tool.app`
   - Or right-click the app → Open (first time only)
4. Launch from Applications

**Windows:**
1. Download the `.exe` file
2. Run the installer
3. Launch from Start Menu or Desktop

### Usage

1. **Connect your ESP32** via USB
2. **Launch the app** - it will auto-detect your board
3. **Click "Flash Firmware"** - downloads latest firmware automatically
4. **Wait for completion** - don't disconnect!

That's it! No Python, no command line, no technical knowledge required.

---

## 🛠️ Building from Source

### Prerequisites

- Node.js 20+
- npm

### Build Instructions

```bash
# Clone the repository
git clone https://github.com/RaceFPV/StarForgeOS.git
cd StarForgeOS/flash_tool

# Install dependencies
npm install

# Download esptool binaries (required for building)
./download-esptool.sh

# Build for your platform
npm run build          # Current platform
npm run build:mac      # macOS only
npm run build:win      # Windows only
npm run build:all      # Both platforms
```

Output will be in `flash_tool/dist/`:
- macOS: `.dmg` and `.zip`
- Windows: `.exe` installer

### Development Mode

```bash
npm start
```

This launches the app without building an installer.

---

## 🤖 Automated Builds (GitHub Actions)

The flasher is automatically built for every release via GitHub Actions.

### How It Works

1. **Push a tag** or **create a release** on GitHub
2. GitHub Actions automatically:
   - Builds firmware for all ESP32 boards (existing workflow)
   - Builds flasher apps for macOS and Windows (new workflow)
   - Uploads all assets to the release
3. Users download and install - zero configuration needed

### What Gets Built

Each release includes:
- **Firmware ZIPs**: 8 board types
  - `StarForge-esp32-c3-supermini.zip`
  - `StarForge-esp32-c3.zip`
  - `StarForge-esp32-c6.zip`
  - `StarForge-esp32dev.zip`
  - `StarForge-esp32-s3.zip`
  - `StarForge-esp32-s3-touch.zip`
  - `StarForge-esp32-s2.zip`
  - `StarForge-jc2432w328c.zip`

- **Flasher Apps**: 2 platforms (new)
  - `StarForge-Flasher-1.0.0.dmg` (macOS)
  - `StarForge-Flasher-Setup-1.0.0.exe` (Windows)

### Trigger a Build

```bash
# Create and push a tag
git tag v1.0.1
git push origin v1.0.1

# Or create a release via GitHub UI
# Go to: https://github.com/RaceFPV/StarForgeOS/releases/new
```

---

## 📦 What's Included

The flasher app is **completely self-contained**:

✅ **esptool binaries** - No Python installation needed  
✅ **All dependencies** - No npm or Node.js needed  
✅ **Serial drivers** - Works out of the box (on most systems)  
✅ **GitHub integration** - Auto-downloads firmware from releases  
✅ **Offline mode** - Can flash from local firmware folder  

Users literally just download, install, and flash. That's it!

---

## 🎯 File Sizes

- **DMG (macOS)**: ~35-40MB
- **EXE (Windows)**: ~40-50MB

Why so large?
- Electron framework (~30MB)
- esptool binaries (~15MB for all platforms)
- Node.js runtime (bundled)
- Chromium rendering engine (bundled)

This is normal for Electron apps and ensures everything works out of the box.

---

## 🔐 Code Signing (Optional but Recommended)

For production releases, you should sign your apps:

### macOS
Requires an Apple Developer account ($99/year):
```bash
export CSC_LINK=/path/to/certificate.p12
export CSC_KEY_PASSWORD=your_password
npm run build:mac
```

### Windows
Requires a code signing certificate:
```bash
export CSC_LINK=/path/to/certificate.pfx
export CSC_KEY_PASSWORD=your_password
npm run build:win
```

Without signing:
- **macOS**: Users may see "app is damaged" error due to Gatekeeper. Workaround:
  ```bash
  xattr -cr /Applications/StarForge\ Flash\ Tool.app
  ```
  Or right-click → Open on first launch.
- **Windows**: SmartScreen warning on first run

**Note:** The latest macOS versions are stricter about unsigned apps. For production releases, proper code signing with an Apple Developer account ($99/year) is strongly recommended to avoid user friction.

---

## 🐛 Troubleshooting Builds

### Build fails on macOS
```bash
# Clear cache and rebuild
rm -rf dist node_modules
npm install
npm run build:mac
```

### Build fails on Windows
```cmd
rmdir /s /q dist node_modules
npm install
npm run build:win
```

### esptool missing
```bash
cd flash_tool
./download-esptool.sh
```

### macOS: "App is damaged and cannot be opened"
This happens because the app isn't code signed (Gatekeeper security). Fix:

**Option 1 (Recommended):** Remove quarantine attribute
```bash
xattr -cr /Applications/StarForge\ Flash\ Tool.app
```

**Option 2:** Right-click the app → Open (first time only)

**Option 3:** Allow in System Settings
1. System Settings → Privacy & Security
2. Scroll to "Security" section
3. Click "Open Anyway" if shown

**Why this happens:** macOS Gatekeeper blocks unsigned apps downloaded from the internet. The app is safe, but macOS requires either:
- Code signing with Apple Developer certificate (requires $99/year account)
- Manual user approval (the workarounds above)

### Can't build Windows on Mac
This should work, but if it fails:
- Use a Windows machine or VM
- Or use GitHub Actions (it will build both platforms)

---

## 📝 Version Numbers

Update version in `flash_tool/package.json`:
```json
{
  "version": "1.0.1"
}
```

This version appears in:
- Installer filename
- App "About" dialog
- macOS app bundle
- Windows installer

---

## 🎉 Distribution Checklist

Before releasing:
- [ ] Test flasher on real hardware
- [ ] Bump version number in `package.json`
- [ ] Update CHANGELOG
- [ ] Create GitHub release
- [ ] Wait for Actions to build installers
- [ ] Test installers on clean Mac/Windows
- [ ] Announce on Discord/social media

---

## 🆘 Support

For issues with:
- **Using the flasher**: https://discord.gg/Ep8sqJmh9d
- **Building from source**: Open an issue
- **Contributing**: Pull requests welcome!

