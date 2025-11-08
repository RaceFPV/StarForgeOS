#!/bin/bash
# StarForgeOS Simple Test Runner
# Tests all boards at once using PlatformIO

set -e

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
BLUE='\033[0;34m'
NC='\033[0m'

echo -e "${BLUE}======================================${NC}"
echo -e "${BLUE}StarForgeOS Multi-Board Tests${NC}"
echo -e "${BLUE}======================================${NC}\n"

# Change to project root
cd "$(dirname "$0")/.."

# Parse mode
MODE="${1:-build-only}"

case $MODE in
    build-only)
        echo -e "${BLUE}Running BUILD-ONLY tests (no hardware)${NC}\n"
        # Test only test-* environments without uploading
        pio test -e test-esp32-c3 -e test-esp32-c6 -e test-esp32 -e test-esp32-s2 -e test-esp32-s3 -e test-esp32-s3-touch -e test-jc2432w328c --without-uploading --without-testing
        ;;
    hardware)
        echo -e "${BLUE}Running HARDWARE tests (board must be connected)${NC}\n"
        # Test all test-* environments with hardware
        pio test -e test-esp32-c3 -e test-esp32-c6 -e test-esp32 -e test-esp32-s2 -e test-esp32-s3 -e test-esp32-s3-touch -e test-jc2432w328c
        ;;
    specific)
        if [ -z "$2" ]; then
            echo -e "${RED}Error: Please specify board environment${NC}"
            echo "Usage: $0 specific <env>"
            echo "Available: test-esp32-c3, test-esp32-c6, test-esp32, etc."
            exit 1
        fi
        echo -e "${BLUE}Testing $2${NC}\n"
        pio test -e "$2" --without-uploading --without-testing
        ;;
    *)
        echo -e "${RED}Unknown mode: $MODE${NC}"
        echo "Usage: $0 [build-only|hardware|specific <env>]"
        exit 1
        ;;
esac

echo -e "\n${GREEN}Tests completed!${NC}"

