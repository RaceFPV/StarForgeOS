#!/bin/bash
# Setup script for JC2432W328C display test

echo "=== JC2432W328C Display Setup ==="
echo ""

# Step 1: Initial build to download libraries
echo "Step 1: Downloading libraries..."
pio run

# Step 2: Check if TFT_eSPI was downloaded
TFESPI_PATH=".pio/libdeps/jc2432w328c/TFT_eSPI"
if [ -d "$TFESPI_PATH" ]; then
    echo "✓ TFT_eSPI library found at: $TFESPI_PATH"
    
    # Step 3: Copy User_Setup.h
    echo ""
    echo "Step 2: Copying User_Setup.h to TFT_eSPI library..."
    cp "src/TFT_eSPI User_Setup.h" "$TFESPI_PATH/User_Setup.h"
    echo "✓ User_Setup.h copied"
    
    # Step 4: Rebuild and upload
    echo ""
    echo "Step 3: Building and uploading firmware..."
    pio run --target upload --upload-port /dev/ttyUSB0
    
    echo ""
    echo "=== Setup Complete ==="
    echo "Monitor output with: pio device monitor --port /dev/ttyUSB0 --baud 115200"
else
    echo "✗ TFT_eSPI library not found. Build may have failed."
    echo "Check the error messages above."
fi

