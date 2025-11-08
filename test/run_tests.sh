#!/bin/bash
# StarForgeOS Test Runner Script
# Run tests across all supported ESP32 board types

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to print colored output
print_header() {
    echo -e "\n${BLUE}========================================${NC}"
    echo -e "${BLUE}$1${NC}"
    echo -e "${BLUE}========================================${NC}\n"
}

print_success() {
    echo -e "${GREEN}✓ $1${NC}"
}

print_error() {
    echo -e "${RED}✗ $1${NC}"
}

print_warning() {
    echo -e "${YELLOW}⚠ $1${NC}"
}

# Check if PlatformIO is installed
if ! command -v pio &> /dev/null; then
    print_error "PlatformIO not found. Please install it:"
    echo "  pip install platformio"
    exit 1
fi

print_success "PlatformIO found"

# Change to project root (parent of test directory)
cd "$(dirname "$0")/.."
PROJECT_DIR=$(pwd)

print_header "StarForgeOS Multi-Board Test Suite"

# Array of test environments
declare -a BOARDS=(
    "test-esp32-c3:ESP32-C3"
    "test-esp32-c6:ESP32-C6"
    "test-esp32:ESP32"
    "test-esp32-s2:ESP32-S2"
    "test-esp32-s3:ESP32-S3"
    "test-esp32-s3-touch:ESP32-S3-Touch"
    "test-jc2432w328c:JC2432W328C"
)

# Test mode
TEST_MODE="${1:-build-only}"  # Default to build-only

case $TEST_MODE in
    build-only)
        print_header "Running BUILD-ONLY tests (no hardware required)"
        print_warning "This validates compilation but does not run tests on hardware"
        ;;
    hardware)
        print_header "Running HARDWARE tests (board must be connected)"
        print_warning "Make sure your ESP32 board is connected via USB"
        ;;
    specific)
        if [ -z "$2" ]; then
            print_error "Please specify a board environment"
            echo "Usage: $0 specific <board>"
            echo "Available boards:"
            for board in "${BOARDS[@]}"; do
                IFS=':' read -r env name <<< "$board"
                echo "  - $env ($name)"
            done
            exit 1
        fi
        SPECIFIC_BOARD="$2"
        print_header "Running tests for $SPECIFIC_BOARD"
        ;;
    *)
        print_error "Unknown test mode: $TEST_MODE"
        echo "Usage: $0 [build-only|hardware|specific <board>]"
        exit 1
        ;;
esac

# Track results
PASSED=0
FAILED=0
declare -a FAILED_BOARDS

# Function to run tests for a board
run_board_tests() {
    local ENV=$1
    local NAME=$2
    
    print_header "Testing $NAME ($ENV)"
    
    if [ "$TEST_MODE" = "build-only" ]; then
        # Build only, no upload/test
        if pio test -e "$ENV" --without-uploading --without-testing; then
            print_success "$NAME build passed"
            ((PASSED++))
        else
            print_error "$NAME build failed"
            ((FAILED++))
            FAILED_BOARDS+=("$NAME")
        fi
    else
        # Full hardware test
        if pio test -e "$ENV"; then
            print_success "$NAME tests passed"
            ((PASSED++))
        else
            print_error "$NAME tests failed"
            ((FAILED++))
            FAILED_BOARDS+=("$NAME")
        fi
    fi
}

# Run tests
if [ "$TEST_MODE" = "specific" ]; then
    # Run specific board
    FOUND=false
    for board in "${BOARDS[@]}"; do
        IFS=':' read -r env name <<< "$board"
        if [ "$env" = "$SPECIFIC_BOARD" ]; then
            run_board_tests "$env" "$name"
            FOUND=true
            break
        fi
    done
    
    if [ "$FOUND" = false ]; then
        print_error "Unknown board: $SPECIFIC_BOARD"
        exit 1
    fi
else
    # Run all boards
    for board in "${BOARDS[@]}"; do
        IFS=':' read -r env name <<< "$board"
        run_board_tests "$env" "$name"
    done
fi

# Print summary
print_header "Test Summary"

echo "Total boards tested: $((PASSED + FAILED))"
print_success "Passed: $PASSED"

if [ $FAILED -gt 0 ]; then
    print_error "Failed: $FAILED"
    echo -e "\nFailed boards:"
    for board in "${FAILED_BOARDS[@]}"; do
        echo "  - $board"
    done
    exit 1
else
    print_success "All tests passed!"
    exit 0
fi

