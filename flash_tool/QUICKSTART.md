# Quick Start Instructions

## 📋 Before You Start

You need to update one thing in the code:

### 1. Update GitHub Repository URL

Edit `main.js` line ~120 and replace `YOUR_USERNAME` with your GitHub username:

```javascript
// Change this:
'https://api.github.com/repos/YOUR_USERNAME/StarForge/releases/latest'

// To this (example):
'https://api.github.com/repos/racefpv/StarForge/releases/latest'
```

## 🚀 Development Setup

### macOS/Linux

```bash
cd flash_tool
./setup.sh
npm start
```

### Windows

```cmd
cd flash_tool
setup.bat
npm start
```

### Manual Setup

```bash
cd flash_tool
npm install
npm start
```

## 📦 Building Installers

### Build for your current platform
```bash
npm run build
```

### Build for specific platform
```bash
npm run build:mac    # macOS DMG
npm run build:win    # Windows installer
```

### Build for all platforms (macOS required for Mac builds)
```bash
npm run build:all
```

Output will be in `dist/` folder.

## 🔧 Requirements

### For Development
- Node.js 18+
- npm or yarn

### For Users (bundled in app)
- esptool.py (install with `pip install esptool`)

### For Testing
- ESP32 board connected via USB
- USB-to-Serial drivers (CP210x or CH340)

## 🧪 Testing the App

1. **Start the app**: `npm start`
2. **Connect ESP32 board** via USB
3. **Select board type** from dropdown
4. **Click "Refresh Ports"** - your board should appear
5. **Test with local firmware**:
   - Build firmware: `cd .. && pio run -e esp32-c3-supermini`
   - Select "Local Folder" in app
   - Browse to `.pio/build/esp32-c3-supermini/`
   - Click "Flash Firmware"

## 🐛 Troubleshooting

### esptool not found
```bash
pip install esptool
```

### No serial ports detected
- Install USB drivers (CP210x or CH340)
- On Linux: Add user to dialout group
  ```bash
  sudo usermod -a -G dialout $USER
  sudo usermod -a -G uucp $USER
  ```
- Restart after driver installation

### Build fails
- Delete `node_modules/` and run `npm install` again
- Make sure you have Node.js 18+
- On macOS for Windows builds, you need Wine

## 📂 Project Structure

```
flash_tool/
├── main.js           # Electron main process
├── preload.js        # Security bridge (contextBridge)
├── index.html        # UI layout
├── renderer.js       # UI logic
├── package.json      # Dependencies & build config
├── resources/        # Bundled files (esptool, etc)
└── dist/            # Build outputs (created on build)
```

## 🎨 Customization

### Change app icon
Replace `assets/icon.png` with your own icon (512x512 PNG)

### Change app name
Edit `package.json`:
```json
"name": "your-app-name",
"productName": "Your App Name"
```

### Add more boards
Edit `BOARD_CONFIGS` in `main.js`

## 🚢 Distribution

After building:

### macOS
- Upload `StarForge Flasher-1.0.0.dmg` to GitHub releases
- Users download and drag to Applications

### Windows  
- Upload `StarForge Flasher Setup 1.0.0.exe` to GitHub releases
- Users download and run installer

### Auto-updates (Advanced)
Add `electron-updater` package for automatic updates from GitHub releases.

## 💡 Tips

- Test on both Mac and Windows before releasing
- Version your releases in package.json
- Create GitHub releases with firmware ZIPs
- The app will auto-download from latest release

## 🔐 Code Signing (Production)

For production apps:
- **macOS**: Sign with Apple Developer certificate
- **Windows**: Sign with code signing certificate

Without signing, users will see security warnings.

## 📝 Next Steps

1. Update GitHub URL in main.js
2. Run `npm install`
3. Test with `npm start`
4. Build with `npm run build`
5. Create a GitHub release with firmware ZIPs
6. Distribute installers to users

