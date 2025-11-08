.PHONY: help test test-all test-board clean build upload monitor format docs

# Default target - show help
help:
	@echo "════════════════════════════════════════════════════════════"
	@echo "  StarForgeOS - ESP32 Multi-Board Lap Timer"
	@echo "════════════════════════════════════════════════════════════"
	@echo ""
	@echo "📋 Available Commands:"
	@echo ""
	@echo "  Testing:"
	@echo "    make test              - Quick build-only test (all 7 boards)"
	@echo "    make test-all          - Run tests on all boards (no hardware)"
	@echo "    make test-board BOARD=<env> - Test specific board with hardware"
	@echo ""
	@echo "  Building:"
	@echo "    make build BOARD=<env> - Build for specific board"
	@echo "    make upload BOARD=<env> - Build and upload to board"
	@echo "    make clean             - Clean build artifacts"
	@echo ""
	@echo "  Development:"
	@echo "    make monitor BOARD=<env> - Open serial monitor"
	@echo "    make size BOARD=<env>    - Show firmware size"
	@echo "    make list-boards         - List all available boards"
	@echo ""
	@echo "  Integration:"
	@echo "    make test-rotorhazard PORT=<port> BOARD=<env> - Test with mock RotorHazard"
	@echo ""
	@echo "  Documentation:"
	@echo "    make docs              - Open test documentation"
	@echo ""
	@echo "🎯 Board Environments:"
	@echo "    esp32-c3-supermini     - ESP32-C3 SuperMini"
	@echo "    esp32-c6               - ESP32-C6 DevKit"
	@echo "    esp32dev               - Generic ESP32"
	@echo "    esp32-s2               - ESP32-S2"
	@echo "    esp32-s3               - ESP32-S3"
	@echo "    esp32-s3-touch         - Waveshare ESP32-S3-Touch-LCD-2"
	@echo "    jc2432w328c            - JC2432W328C LCD board"
	@echo ""
	@echo "💡 Examples:"
	@echo "    make test                                  # Quick validation"
	@echo "    make build BOARD=esp32-c3-supermini"
	@echo "    make upload BOARD=esp32dev"
	@echo "    make test-board BOARD=test-esp32-c3"
	@echo "    make test-rotorhazard PORT=/dev/ttyUSB0 BOARD=esp32dev"
	@echo ""

# Quick test - build only (no hardware required)
test:
	@echo "🔧 Running quick build-only tests (all boards)..."
	@./test/run_tests_simple.sh build-only

# Test all boards (build only)
test-all:
	@echo "🔧 Testing all board configurations..."
	@pio test --without-uploading --without-testing \
		-e test-esp32-c3 \
		-e test-esp32-c6 \
		-e test-esp32 \
		-e test-esp32-s2 \
		-e test-esp32-s3 \
		-e test-esp32-s3-touch \
		-e test-jc2432w328c

# Test specific board with hardware
test-board:
ifndef BOARD
	@echo "❌ Error: BOARD not specified"
	@echo "Usage: make test-board BOARD=test-esp32-c3"
	@exit 1
endif
	@echo "🔌 Testing $(BOARD) with hardware..."
	@pio test -e $(BOARD)

# Build for specific board
build:
ifndef BOARD
	@echo "❌ Error: BOARD not specified"
	@echo "Usage: make build BOARD=esp32-c3-supermini"
	@exit 1
endif
	@echo "🔨 Building for $(BOARD)..."
	@pio run -e $(BOARD)

# Build and upload to board
upload:
ifndef BOARD
	@echo "❌ Error: BOARD not specified"
	@echo "Usage: make upload BOARD=esp32-c3-supermini"
	@exit 1
endif
	@echo "📤 Building and uploading to $(BOARD)..."
	@pio run -e $(BOARD) -t upload

# Open serial monitor
monitor:
ifndef BOARD
	@echo "❌ Error: BOARD not specified"
	@echo "Usage: make monitor BOARD=esp32-c3-supermini"
	@exit 1
endif
	@echo "📺 Opening serial monitor for $(BOARD)..."
	@pio device monitor -e $(BOARD)

# Show firmware size
size:
ifndef BOARD
	@echo "❌ Error: BOARD not specified"
	@echo "Usage: make size BOARD=esp32-c3-supermini"
	@exit 1
endif
	@pio run -e $(BOARD) -t size

# Clean build artifacts
clean:
	@echo "🧹 Cleaning build artifacts..."
	@pio run -t clean
	@rm -rf .pio
	@echo "✅ Clean complete"

# List all available boards
list-boards:
	@echo "📋 Available Production Boards:"
	@pio project config --json-output | grep -o '"env:[^"]*"' | sed 's/"env://g' | sed 's/"//g' | grep -v "^test-" || echo "  (run from StarForgeOS directory)"
	@echo ""
	@echo "📋 Available Test Boards:"
	@pio project config --json-output | grep -o '"env:[^"]*"' | sed 's/"env://g' | sed 's/"//g' | grep "^test-" || echo "  (run from StarForgeOS directory)"

# Open documentation
docs:
	@echo "📖 Opening test documentation..."
	@if command -v xdg-open > /dev/null; then \
		xdg-open test/INDEX.md; \
	elif command -v open > /dev/null; then \
		open test/INDEX.md; \
	else \
		echo "View documentation at: test/INDEX.md"; \
	fi

# Install development dependencies
install:
	@echo "📦 Installing PlatformIO..."
	@pip install -U platformio
	@echo "✅ Installation complete"

# Update PlatformIO
update:
	@echo "🔄 Updating PlatformIO..."
	@pio upgrade
	@pio update
	@echo "✅ Update complete"

# Format code (if clang-format is available)
format:
	@echo "🎨 Formatting code..."
	@if command -v clang-format > /dev/null; then \
		find src -name "*.cpp" -o -name "*.h" | xargs clang-format -i; \
		find test -name "*.cpp" -o -name "*.h" | xargs clang-format -i; \
		echo "✅ Code formatted"; \
	else \
		echo "⚠️  clang-format not installed (optional)"; \
	fi

# Quick development cycle: clean, build, upload, monitor
dev:
ifndef BOARD
	@echo "❌ Error: BOARD not specified"
	@echo "Usage: make dev BOARD=esp32-c3-supermini"
	@exit 1
endif
	@echo "🚀 Development cycle for $(BOARD)..."
	@$(MAKE) clean
	@$(MAKE) build BOARD=$(BOARD)
	@$(MAKE) upload BOARD=$(BOARD)
	@$(MAKE) monitor BOARD=$(BOARD)

# Show board info
info:
ifndef BOARD
	@echo "❌ Error: BOARD not specified"
	@echo "Usage: make info BOARD=esp32-c3-supermini"
	@exit 1
endif
	@echo "ℹ️  Board Information for $(BOARD):"
	@pio boards $(BOARD) 2>/dev/null || echo "Board details not available"

# CI/CD simulation (what GitHub Actions will run)
ci:
	@echo "🤖 Simulating CI/CD pipeline..."
	@$(MAKE) test
	@echo "✅ CI simulation complete"

# Show disk usage of build artifacts
du:
	@echo "💾 Build artifacts disk usage:"
	@du -sh .pio 2>/dev/null || echo "No build artifacts found"

# RotorHazard integration test (requires hardware)
test-rotorhazard:
ifndef PORT
	@echo "❌ Error: PORT not specified"
	@echo "Usage: make test-rotorhazard PORT=/dev/ttyUSB0 BOARD=esp32-c3-supermini"
	@exit 1
endif
ifndef BOARD
	@echo "❌ Error: BOARD not specified"
	@echo "Usage: make test-rotorhazard PORT=/dev/ttyUSB0 BOARD=esp32-c3-supermini"
	@exit 1
endif
	@echo "🔌 Running RotorHazard integration test..."
	@chmod +x test/tools/test_rotorhazard_integration.sh
	@./test/tools/test_rotorhazard_integration.sh $(PORT) $(BOARD)

