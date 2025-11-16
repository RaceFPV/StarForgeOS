#!/bin/bash

# Download mkspiffs binaries for bundling with the Electron app
# Downloads pre-built binaries from GitHub releases

echo "📦 Downloading mkspiffs binaries..."
echo ""

RESOURCES_DIR="$(dirname "$0")/resources/bin"
mkdir -p "$RESOURCES_DIR"

MKSPIFFS_VERSION="0.2.3"
BASE_URL="https://github.com/igrr/mkspiffs/releases/download/${MKSPIFFS_VERSION}"

echo "Creating resources directory: $RESOURCES_DIR"
echo ""

# Determine platform and download appropriate binary
if [[ "$OSTYPE" == "darwin"* ]]; then
    echo "⬇️  Downloading mkspiffs for macOS..."
    DOWNLOAD_URL="${BASE_URL}/mkspiffs-${MKSPIFFS_VERSION}-arduino-esp32-osx.tar.gz"
    TARGET_NAME="mkspiffs-macos"
    TEMP_DIR="/tmp/mkspiffs-mac-$$"
    mkdir -p "$TEMP_DIR"
    
    if curl -L "$DOWNLOAD_URL" -o "$TEMP_DIR/mkspiffs.tar.gz" 2>/dev/null; then
        tar -xzf "$TEMP_DIR/mkspiffs.tar.gz" -C "$TEMP_DIR" 2>/dev/null
        MKSPIFFS_BINARY=$(find "$TEMP_DIR" -name "mkspiffs" -type f | head -1)
        if [ -n "$MKSPIFFS_BINARY" ]; then
            cp "$MKSPIFFS_BINARY" "$RESOURCES_DIR/$TARGET_NAME"
            chmod +x "$RESOURCES_DIR/$TARGET_NAME"
            echo "✅ macOS binary downloaded: $RESOURCES_DIR/$TARGET_NAME"
            rm -rf "$TEMP_DIR"
        else
            echo "❌ Could not find mkspiffs in archive"
            rm -rf "$TEMP_DIR"
            exit 1
        fi
    else
        echo "❌ Failed to download macOS binary"
        rm -rf "$TEMP_DIR"
        exit 1
    fi
    
elif [[ "$OSTYPE" == "linux-gnu"* ]]; then
    echo "⬇️  Downloading mkspiffs for Linux..."
    DOWNLOAD_URL="${BASE_URL}/mkspiffs-${MKSPIFFS_VERSION}-arduino-esp32-linux64.tar.gz"
    TARGET_NAME="mkspiffs-linux"
    TEMP_DIR="/tmp/mkspiffs-linux-$$"
    mkdir -p "$TEMP_DIR"
    
    if curl -L "$DOWNLOAD_URL" -o "$TEMP_DIR/mkspiffs.tar.gz" 2>/dev/null; then
        tar -xzf "$TEMP_DIR/mkspiffs.tar.gz" -C "$TEMP_DIR" 2>/dev/null
        MKSPIFFS_BINARY=$(find "$TEMP_DIR" -name "mkspiffs" -type f | head -1)
        if [ -n "$MKSPIFFS_BINARY" ]; then
            cp "$MKSPIFFS_BINARY" "$RESOURCES_DIR/$TARGET_NAME"
            chmod +x "$RESOURCES_DIR/$TARGET_NAME"
            echo "✅ Linux binary downloaded: $RESOURCES_DIR/$TARGET_NAME"
            rm -rf "$TEMP_DIR"
        else
            echo "❌ Could not find mkspiffs in archive"
            rm -rf "$TEMP_DIR"
            exit 1
        fi
    else
        echo "❌ Failed to download Linux binary"
        rm -rf "$TEMP_DIR"
        exit 1
    fi
    
elif [[ "$OSTYPE" == "msys" || "$OSTYPE" == "win32" ]]; then
    echo "⬇️  Downloading mkspiffs for Windows..."
    DOWNLOAD_URL="${BASE_URL}/mkspiffs-${MKSPIFFS_VERSION}-arduino-esp32-win32.zip"
    TARGET_NAME="mkspiffs-win64.exe"
    TEMP_DIR="/tmp/mkspiffs-win-$$"
    mkdir -p "$TEMP_DIR"
    
    if curl -L "$DOWNLOAD_URL" -o "$TEMP_DIR/mkspiffs.zip" 2>/dev/null; then
        unzip -o -q "$TEMP_DIR/mkspiffs.zip" -d "$TEMP_DIR" 2>/dev/null
        MKSPIFFS_BINARY=$(find "$TEMP_DIR" -name "mkspiffs.exe" -o -name "mkspiffs" | head -1)
        if [ -n "$MKSPIFFS_BINARY" ]; then
            cp "$MKSPIFFS_BINARY" "$RESOURCES_DIR/$TARGET_NAME"
            echo "✅ Windows binary downloaded: $RESOURCES_DIR/$TARGET_NAME"
            rm -rf "$TEMP_DIR"
        else
            echo "❌ Could not find mkspiffs in archive"
            rm -rf "$TEMP_DIR"
            exit 1
        fi
    else
        echo "❌ Failed to download Windows binary"
        rm -rf "$TEMP_DIR"
        exit 1
    fi
else
    echo "❌ Unsupported platform: $OSTYPE"
    exit 1
fi

echo ""
echo "✅ Done! mkspiffs binary ready for bundling."
echo "   Location: $RESOURCES_DIR/$TARGET_NAME"
echo ""
echo "📝 For cross-platform builds, download binaries for other platforms:"
echo "   macOS:   ${BASE_URL}/mkspiffs-${MKSPIFFS_VERSION}-arduino-esp32-osx.tar.gz"
echo "   Linux:  ${BASE_URL}/mkspiffs-${MKSPIFFS_VERSION}-arduino-esp32-linux64.tar.gz"
echo "   Windows: ${BASE_URL}/mkspiffs-${MKSPIFFS_VERSION}-arduino-esp32-win32.zip"

