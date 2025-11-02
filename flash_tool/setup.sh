#!/bin/bash

# StarForge Flasher - Quick Development Setup Script

echo "🚀 StarForge Flasher Setup"
echo "=========================="
echo ""

# Check Node.js
if ! command -v node &> /dev/null; then
    echo "❌ Node.js not found. Please install Node.js 18+ from https://nodejs.org"
    exit 1
fi

echo "✅ Node.js $(node --version) found"

# Check npm
if ! command -v npm &> /dev/null; then
    echo "❌ npm not found"
    exit 1
fi

echo "✅ npm $(npm --version) found"

# Check esptool
if ! command -v esptool.py &> /dev/null && ! command -v esptool &> /dev/null; then
    echo "⚠️  esptool not found in PATH"
    echo "   Install with: pip install esptool"
    echo "   The app will still run but flashing won't work until esptool is installed"
else
    echo "✅ esptool found"
fi

echo ""
echo "📦 Installing npm dependencies..."
npm install

if [ $? -eq 0 ]; then
    echo ""
    echo "✅ Setup complete!"
    echo ""
    echo "Available commands:"
    echo "  npm start      - Run in development mode"
    echo "  npm run build  - Build for your platform"
    echo "  npm run build:mac - Build for macOS"
    echo "  npm run build:win - Build for Windows"
    echo ""
    echo "To get started, run:"
    echo "  npm start"
else
    echo "❌ Setup failed. Please check errors above."
    exit 1
fi

