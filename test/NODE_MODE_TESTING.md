# Node Mode / RotorHazard Testing

Two complementary approaches for testing RotorHazard compatibility.

## 🎯 Test Types

### 1. Protocol Unit Tests (No Hardware) ⚡

**File**: `test/test_protocol/test_node_protocol.cpp`

**What it tests**:
- ✅ Protocol command formats are correct
- ✅ Checksum calculations work
- ✅ Frequency encoding/decoding
- ✅ Command payload sizes
- ✅ Buffer bounds
- ✅ API level compatibility

**When to run**: Every build (included in `make test`)

**How to run**:
```bash
# Automatic - runs with all tests
make test

# Or specifically
cd StarForgeOS
pio test -e test-esp32-c3 -f test_protocol --without-uploading --without-testing
```

**Time**: ~2 seconds  
**Hardware needed**: ❌ None  
**CI-Friendly**: ✅ Yes  

**What it catches**:
```cpp
// Example: If you change TimingCore API
class TimingCore {
    // Old: uint16_t getFrequency()
    uint32_t getFrequency()  // ← Protocol test catches this!
};

// Example: If checksum logic breaks
uint8_t checksum = data[0] ^ data[1];  // Wrong! Should be +, not ^
```

---

### 2. Integration Tests (With Hardware) 🔌

**File**: `test/tools/mock_rotorhazard.py`

**What it tests**:
- ✅ Actual serial communication works
- ✅ Baud rate is correct
- ✅ Commands receive responses
- ✅ RSSI reading functions
- ✅ Frequency changes work on hardware

**When to run**: Before releases, after major changes

**How to run**:
```bash
make test-rotorhazard PORT=/dev/ttyUSB0 BOARD=esp32dev
```

**Time**: ~10 seconds  
**Hardware needed**: ✅ Real ESP32  
**CI-Friendly**: ⚠️ Only with self-hosted runner  

**What it catches**:
- Serial timing issues
- Hardware-specific bugs
- Actual communication failures
- Real-world protocol behavior

---

## Recommended Workflow

### Daily Development
```bash
# Always run before committing
make test  # Includes protocol tests, no hardware needed
```

### Before Pull Request
```bash
# Run protocol tests
make test  # Fast validation

# If you have hardware, test one board
make test-rotorhazard PORT=/dev/ttyUSB0 BOARD=esp32dev
```

### Before Release
```bash
# Test all boards (if you have them)
for board in esp32-c3-supermini esp32dev esp32-s3; do
    make test-rotorhazard PORT=/dev/ttyUSB0 BOARD=$board
done

# Or test with actual RotorHazard server
# (See tools/README_ROTORHAZARD.md)
```

---

## Comparison

| Aspect | Protocol Unit Tests | Integration Tests | Full RH |
|--------|-------------------|------------------|---------|
| **Hardware** | ❌ None | ✅ ESP32 | ✅ ESP32 |
| **Server** | ❌ None | ❌ Mock script | ✅ Full server |
| **Speed** | ⚡ 2 sec | ⚡ 10 sec | 🐌 5 min |
| **CI-Friendly** | ✅ Yes | ⚠️ Self-hosted only | ❌ No |
| **Catches** | Logic bugs | Comm bugs | Everything |
| **Confidence** | 🟡 70% | 🟢 90% | 🟢 100% |

---

## What Each Test Catches

### Protocol Unit Tests Catch:
```cpp
// ✅ Wrong checksum algorithm
checksum = data[0] ^ data[1];  // Should be +

// ✅ Wrong byte order
uint16_t freq = (low << 8) | high;  // Should be reversed

// ✅ Buffer overflow
uint8_t buffer[16];  // Protocol needs 32!

// ✅ Invalid command codes
#define READ_FREQUENCY 0x99  // Wrong! Should be 0x03

// ✅ API level mismatch
#define NODE_API_LEVEL 42  // Wrong! Should be 35
```

### Integration Tests Catch:
```cpp
// ✅ Timing issues
delay(1);  // Too fast, server misses response

// ✅ Serial config
Serial.begin(115200);  // Wrong! Should be 921600

// ✅ Hardware-specific bugs
#ifdef ESP32_C3
    // Missing code path
#endif
```

### Full RotorHazard Catches:
- Race management integration
- Multi-node coordination
- Database storage
- Web interface interaction
- Long-running stability

---

## CI/CD Integration

### GitHub Actions (Automatic)
```yaml
# .github/workflows/build-validation.yml already includes this!
- name: Run build validation
  run: make test  # ← Protocol tests included
```

Protocol tests run automatically on every PR! ✅

### Self-Hosted Runner (Optional)
```yaml
# Add to workflow if you have hardware
- name: Integration Test
  if: runner.self-hosted
  run: make test-rotorhazard PORT=/dev/ttyUSB0 BOARD=esp32dev
```

---

## Development Examples

### Example 1: Changing Protocol Logic
```bash
# Edit node_mode.cpp
vim src/node_mode.cpp

# Run protocol tests (2 seconds, no hardware)
make test

# If passes, you're probably safe!
# Before release, test with hardware:
make test-rotorhazard PORT=/dev/ttyUSB0 BOARD=esp32dev
```

### Example 2: Changing TimingCore API
```bash
# Edit timing_core.h
vim src/timing_core.h

# Protocol tests will catch if node_mode breaks
make test  # ← Will fail if contract broken!

# Fix node_mode.cpp to match new API
# Re-run until tests pass
```

### Example 3: Adding New Command
```bash
# 1. Add to node_mode.cpp
#define READ_NEW_FEATURE 0x50

# 2. Add protocol test
vim test/test_protocol/test_node_protocol.cpp
void test_new_feature_command() {
    TEST_ASSERT_EQUAL(0x50, READ_NEW_FEATURE);
}

# 3. Validate
make test

# 4. Test with hardware
make test-rotorhazard PORT=/dev/ttyUSB0 BOARD=esp32dev
```

---

## Summary

✅ **Use protocol tests** (automatic, fast, no hardware)  
✅ **Use integration tests** before releases (with hardware)  
✅ **Use full RotorHazard** occasionally for comprehensive validation  

The protocol tests give you **fast feedback** during development, catching most bugs before you even need hardware! 🚀

---

**Related Files**:
- `test/test_protocol/test_node_protocol.cpp` - Protocol unit tests
- `test/tools/mock_rotorhazard.py` - Integration test mock
- `test/tools/README_ROTORHAZARD.md` - Integration test docs
- `src/node_mode.cpp` - Protocol implementation

