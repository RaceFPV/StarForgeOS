# esptool Resources

This folder should contain esptool binaries for different platforms:

## Required Files

- `esptool.py` - Python script (or standalone executable)
- Windows: `esptool.exe` (optional, for standalone Windows build)
- macOS/Linux: Use system esptool via pip install

## Installation

### Option 1: System-wide (Recommended)

Users install esptool via pip:
```bash
pip install esptool
```

### Option 2: Bundle with App (Advanced)

To bundle esptool with the app:

1. Download esptool.py from: https://github.com/espressif/esptool
2. Place in this folder
3. Update main.js to use bundled version

For Windows standalone:
1. Download esptool.exe from espressif releases
2. Place in this folder
3. App will automatically use it

## Auto-detection

The app automatically detects:
1. System-installed esptool (from PATH)
2. Bundled esptool in resources folder
3. Falls back gracefully with error message

