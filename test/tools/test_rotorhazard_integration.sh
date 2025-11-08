#!/bin/bash
# StarForgeOS RotorHazard Integration Test
# Tests node mode communication with mock RotorHazard server

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}RotorHazard Integration Test${NC}"
echo -e "${BLUE}========================================${NC}\n"

# Check for serial port argument
if [ -z "$1" ]; then
    echo -e "${RED}Error: No serial port specified${NC}"
    echo "Usage: $0 <serial_port> <board_env>"
    echo "Example: $0 /dev/ttyUSB0 esp32-c3-supermini"
    exit 1
fi

SERIAL_PORT=$1
BOARD_ENV=${2:-esp32-c3-supermini}

echo -e "${BLUE}Configuration:${NC}"
echo "  Serial Port: $SERIAL_PORT"
echo "  Board: $BOARD_ENV"
echo ""

# Check if Python is available
if ! command -v python3 &> /dev/null; then
    echo -e "${RED}Python 3 not found${NC}"
    exit 1
fi

# Install pyserial if not present
if ! python3 -c "import serial" 2>/dev/null; then
    echo -e "${YELLOW}Installing pyserial...${NC}"
    pip3 install pyserial
fi

# Build firmware in node mode
echo -e "${BLUE}Step 1: Building firmware...${NC}"
pio run -e $BOARD_ENV

# Upload firmware
echo -e "\n${BLUE}Step 2: Uploading firmware...${NC}"
pio run -e $BOARD_ENV -t upload --upload-port $SERIAL_PORT

# Wait for ESP32 to boot
echo -e "\n${BLUE}Step 3: Waiting for ESP32 to boot...${NC}"
sleep 3

# Run mock RotorHazard test
echo -e "\n${BLUE}Step 4: Running protocol tests...${NC}"
python3 test/tools/mock_rotorhazard.py $SERIAL_PORT

if [ $? -eq 0 ]; then
    echo -e "\n${GREEN}✓ All tests passed!${NC}"
    exit 0
else
    echo -e "\n${RED}✗ Tests failed${NC}"
    exit 1
fi

