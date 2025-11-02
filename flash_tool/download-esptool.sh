#!/bin/bash

# Download standalone esptool binaries for bundling with the Electron app
# This script downloads esptool executables for macOS, Windows, and Linux

echo "📦 Downloading standalone esptool binaries..."
echo ""

RESOURCES_DIR="$(dirname "$0")/resources/bin"
mkdir -p "$RESOURCES_DIR"

ESPTOOL_VERSION="v4.7.0"
BASE_URL="https://github.com/espressif/esptool/releases/download/${ESPTOOL_VERSION}"

echo "Creating resources directory: $RESOURCES_DIR"
echo ""

# Download for macOS (universal)
echo "⬇️  Downloading esptool for macOS..."
TEMP_MAC="/tmp/esptool-mac-$$"
mkdir -p "$TEMP_MAC"
curl -L "${BASE_URL}/esptool-${ESPTOOL_VERSION}-macos.zip" -o "$TEMP_MAC/esptool.zip"
unzip -o -q "$TEMP_MAC/esptool.zip" -d "$TEMP_MAC"
MACOS_BINARY=$(find "$TEMP_MAC" -name "esptool" -type f | head -1)
if [ -n "$MACOS_BINARY" ]; then
  cp "$MACOS_BINARY" "$RESOURCES_DIR/esptool-macos"
  chmod +x "$RESOURCES_DIR/esptool-macos"
  echo "✅ macOS binary copied"
else
  echo "❌ macOS binary not found"
fi
rm -rf "$TEMP_MAC"

# Download for Windows (x64)
echo "⬇️  Downloading esptool for Windows..."
TEMP_WIN="/tmp/esptool-win-$$"
mkdir -p "$TEMP_WIN"
curl -L "${BASE_URL}/esptool-${ESPTOOL_VERSION}-win64.zip" -o "$TEMP_WIN/esptool.zip"
unzip -o -q "$TEMP_WIN/esptool.zip" -d "$TEMP_WIN"
WIN_BINARY=$(find "$TEMP_WIN" -name "esptool.exe" -type f | head -1)
if [ -n "$WIN_BINARY" ]; then
  cp "$WIN_BINARY" "$RESOURCES_DIR/esptool-win64.exe"
  echo "✅ Windows binary copied"
else
  echo "❌ Windows binary not found"
fi
rm -rf "$TEMP_WIN"

# Download for Linux (x64)
echo "⬇️  Downloading esptool for Linux..."
TEMP_LINUX="/tmp/esptool-linux-$$"
mkdir -p "$TEMP_LINUX"
curl -L "${BASE_URL}/esptool-${ESPTOOL_VERSION}-linux-amd64.zip" -o "$TEMP_LINUX/esptool.zip"
unzip -o -q "$TEMP_LINUX/esptool.zip" -d "$TEMP_LINUX"
LINUX_BINARY=$(find "$TEMP_LINUX" -name "esptool" -type f | head -1)
if [ -n "$LINUX_BINARY" ]; then
  cp "$LINUX_BINARY" "$RESOURCES_DIR/esptool-linux"
  chmod +x "$RESOURCES_DIR/esptool-linux"
  echo "✅ Linux binary copied"
else
  echo "❌ Linux binary not found"
fi
rm -rf "$TEMP_LINUX"

echo ""
echo "📦 Downloaded esptool binaries:"
ls -lh "$RESOURCES_DIR"
echo ""
echo "🎉 These binaries will be bundled with your Electron app."
echo "   No Python or pip installation required for end users!"



