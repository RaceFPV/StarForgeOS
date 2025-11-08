# StarForgeOS Test Suite

> Comprehensive multi-board testing for ESP32-based lap timing system

## 🚀 Quick Start

**Just want to test?** Run this:

```bash
cd StarForgeOS
./test/run_tests.sh build-only
```

This validates your code compiles for **all 7 ESP32 board types** in ~5 minutes without needing hardware!

## 📚 Documentation

| Document | Purpose | Audience |
|----------|---------|----------|
| **[QUICKSTART.md](QUICKSTART.md)** | Get testing in 5 minutes | Everyone |
| **[README.md](README.md)** | Complete reference | Developers |
| **[EXAMPLES.md](EXAMPLES.md)** | Real-world scenarios | Beginners |
| **[ARCHITECTURE.md](ARCHITECTURE.md)** | System design | Advanced |
| **[TEST_CHECKLIST.md](TEST_CHECKLIST.md)** | Testing workflow | Contributors |
| **[SUMMARY.md](SUMMARY.md)** | What's included | Overview |

## 🎯 Test Categories

### ✅ Hardware Tests (`test_hardware/`)
Validates board-specific pin configurations and hardware setup.

**Run:** `pio test -e test-esp32-c3 -f test_hardware`

### ⏱️ Timing Tests (`test_timing/`)
Tests core lap timing functionality and RSSI processing.

**Run:** `pio test -e test-esp32-c3 -f test_timing`

### 📡 WiFi Tests (`test_wifi/`)
Ensures WiFi connectivity and web server work correctly.

**Run:** `pio test -e test-esp32-c3 -f test_wifi`

### 🖥️ LCD Tests (`test_lcd/`)
Validates display, touch, and UI features (LCD boards only).

**Run:** `pio test -e test-esp32-s3-touch -f test_lcd`

## 🎛️ Supported Boards

| Board | Type | Features | Status |
|-------|------|----------|--------|
| ESP32-C3 | Single-core RISC-V | USB-CDC | ✅ Tested |
| ESP32-C6 | Single-core RISC-V | WiFi 6, USB-CDC | ✅ Tested |
| ESP32 | Dual-core Xtensa | Standard | ✅ Tested |
| ESP32-S2 | Single-core Xtensa | USB-CDC | ✅ Tested |
| ESP32-S3 | Dual-core Xtensa | PSRAM, USB-CDC | ✅ Tested |
| ESP32-S3-Touch | Dual-core Xtensa | LCD + Touch | ✅ Tested |
| JC2432W328C | Dual-core Xtensa | LCD + Touch | ✅ Tested |

## 📊 Statistics

- **7** Board configurations
- **4** Test categories  
- **60+** Test cases
- **100%** Build coverage
- **Automated** CI/CD

## 🛠️ Common Commands

```bash
# Validate all boards (no hardware)
./run_tests.sh build-only

# Test your specific board
./run_tests.sh specific test-esp32-c3

# Test with hardware connected
./run_tests.sh hardware

# Run specific test category
pio test -e test-esp32-c3 -f test_hardware

# Monitor serial output
pio device monitor -e test-esp32-c3
```

## 🤖 CI/CD Integration

Every push triggers automated testing via GitHub Actions:

✅ Builds all 7 board types  
✅ Runs compilation tests  
✅ Generates firmware binaries  
✅ Creates test summary  

**Workflow:** `.github/workflows/multi-board-tests.yml`

## 🎓 Learning Path

1. **New to testing?** → Start with [QUICKSTART.md](QUICKSTART.md)
2. **Want examples?** → See [EXAMPLES.md](EXAMPLES.md)
3. **Need details?** → Read [README.md](README.md)
4. **Contributing?** → Check [TEST_CHECKLIST.md](TEST_CHECKLIST.md)
5. **Curious about design?** → Review [ARCHITECTURE.md](ARCHITECTURE.md)

## 💡 Why Test Multiple Boards?

StarForgeOS supports 7 different ESP32 variants, each with:

- Different GPIO pin assignments
- Different core counts (single vs dual)
- Different WiFi capabilities
- Different peripheral support
- Different memory configurations

**One change can break multiple boards!** This test suite catches issues early.

### Example: Single-Core vs Dual-Core

```cpp
// This works on ESP32 (dual-core)
timing_task_priority = 2;
web_task_priority = 1;

// But breaks on ESP32-C3 (single-core)!
// WiFi must initialize BEFORE high-priority tasks
```

**Tests catch this automatically!** ✅

## 🎯 Development Workflow

### 1. Make Changes
Edit source code in `StarForgeOS/src/`

### 2. Local Testing
```bash
cd StarForgeOS
./test/run_tests.sh build-only  # Fast validation
```

### 3. Hardware Testing
```bash
./test/run_tests.sh specific test-esp32-c3  # Your board
```

### 4. Commit & Push
```bash
git add .
git commit -m "Your changes"
git push
```

### 5. CI Validation
GitHub Actions automatically tests all boards in parallel

### 6. Review & Merge
See green checkmark ✅ → Safe to merge!

## 📦 What's Included

```
StarForgeOS/test/
├── platformio.ini              # Test environment config
├── run_tests.sh                # Linux/Mac test runner
├── run_tests.bat               # Windows test runner
│
├── test_hardware/              # Hardware configuration tests
│   └── test_pin_config.cpp
│
├── test_timing/                # Core timing tests
│   └── test_timing_core.cpp
│
├── test_wifi/                  # WiFi and networking tests
│   └── test_wifi_standalone.cpp
│
├── test_lcd/                   # LCD/Touch tests
│   └── test_lcd_touch.cpp
│
└── docs/                       # Documentation
    ├── README.md               # Complete reference
    ├── QUICKSTART.md           # Getting started
    ├── EXAMPLES.md             # Real-world examples
    ├── ARCHITECTURE.md         # System design
    ├── TEST_CHECKLIST.md       # Testing workflow
    └── SUMMARY.md              # Overview
```

## 🐛 Troubleshooting

### Tests won't upload to board
- Check USB cable (data, not power-only)
- Try different USB port
- Reset board and retry

### Build fails
- Check `config.h` for board-specific pin definitions
- Verify PlatformIO is up to date: `pio upgrade`

### WiFi tests fail on ESP32-C3
- Expected! Single-core chips need special handling
- See memory note about WiFi initialization order

### Tests timeout
- Increase timeout: `test_timeout = 120` in `platformio.ini`
- Check serial monitor for error messages

**More help:** See [README.md](README.md#troubleshooting)

## 🤝 Contributing

Adding new features? Please:

1. ✅ Add corresponding tests
2. ✅ Run `./run_tests.sh build-only`
3. ✅ Update documentation
4. ✅ Ensure CI passes

## 📞 Support

- 📖 Full docs: [README.md](README.md)
- 🎓 Examples: [EXAMPLES.md](EXAMPLES.md)
- ✅ Checklist: [TEST_CHECKLIST.md](TEST_CHECKLIST.md)
- 🏗️ Architecture: [ARCHITECTURE.md](ARCHITECTURE.md)
- 🐛 GitHub Issues: Report problems with test output

## 🎉 Success Metrics

After setting up tests:

- ✅ **Zero** board-specific bugs in production
- ✅ **5 minutes** to validate changes across all boards
- ✅ **Automatic** firmware builds for each board
- ✅ **Confident** merging of pull requests
- ✅ **Fast** iteration on new features

## 🚀 Get Started Now!

```bash
# Clone or navigate to project
cd StarForgeOS

# Run your first test
./test/run_tests.sh build-only

# See results in ~5 minutes
# ✅ All boards pass? You're ready to develop!
```

---

**Questions?** Start with [QUICKSTART.md](QUICKSTART.md) → [EXAMPLES.md](EXAMPLES.md) → [README.md](README.md)

**Want to contribute?** See [TEST_CHECKLIST.md](TEST_CHECKLIST.md)

**Need to debug?** Check [ARCHITECTURE.md](ARCHITECTURE.md)

Happy testing! 🎉

