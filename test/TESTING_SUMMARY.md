# What Gets Tested By Default?

## Quick Answer

| Command | Unit Tests Compiled? | Unit Tests Run? | Hardware Test Run? |
|---------|---------------------|-----------------|-------------------|
| `make test` | ✅ Yes | ❌ No | ❌ No |
| `make test-all` | ✅ Yes | ❌ No | ❌ No |
| `make test-board BOARD=test-esp32-c3` | ✅ Yes | ✅ Yes | ❌ No |
| `make test-silent PORT=/dev/ttyUSB0` | N/A | N/A | ✅ Yes |

## Detailed Breakdown

### `make test` (Default)

**What happens:**
```bash
pio test --without-uploading --without-testing
```

**Result:**
- ✅ Compiles all test files (including `test_silent_operation.cpp`)
- ✅ Verifies code builds for all 7 board types
- ❌ Does NOT run any tests
- ❌ Does NOT require hardware

**Use case:** Quick validation during development (CI/CD)

### `make test-all` (Same as `make test`)

**What happens:**
```bash
pio test --without-uploading --without-testing
```

**Result:** Same as `make test`

### `make test-board BOARD=test-esp32-c3` (Run Unit Tests)

**What happens:**
```bash
pio test -e test-esp32-c3
```

**Result:**
- ✅ Compiles all tests for the specified board
- ✅ **Uploads and runs unit tests on hardware**
- ✅ Runs `test_silent_operation.cpp` unit tests
- ❌ Does NOT run Python hardware integration test
- ✅ Requires hardware connected

**Output includes:**
```
test/test_protocol/test_silent_operation.cpp:
    test_config_loader_load_default_mode_is_silent [PASSED]
    test_config_loader_load_custom_config_is_silent [PASSED]
    test_config_loader_save_custom_config_is_silent [PASSED]
    test_early_boot_minimal_output [PASSED]
    test_protocol_responses_are_binary [PASSED]
    ...
```

### `make test-silent PORT=/dev/ttyUSB0` (Hardware Integration Test)

**What happens:**
```bash
python3 test/tools/test_silent_rotorhazard.py /dev/ttyUSB0
```

**Result:**
- ✅ Connects to flashed device via serial
- ✅ Sends protocol commands
- ✅ **Verifies NO debug text output**
- ✅ Validates binary protocol responses only
- ✅ Requires hardware with firmware already flashed

**Output:**
```
============================================================
StarForgeOS RotorHazard Silent Mode Test
============================================================
✓ Connected to /dev/ttyUSB0
✓ Boot complete, buffer cleared

[TEST] READ_ADDRESS (should return 1 byte, no debug)
✓ Got API level: 35 (correct)
...
✅ ALL TESTS PASSED - RotorHazard mode is SILENT
```

## Complete Testing Workflow

### 1. During Development (Fast)
```bash
make test  # Just verify it compiles (~2 minutes)
```

### 2. Before Commit (Thorough - Unit Tests)
```bash
make test-board BOARD=test-esp32-c3  # Run unit tests with hardware
```

### 3. Before Release (Critical - Integration Test)
```bash
# Flash device in RotorHazard mode
make upload BOARD=esp32-c3-supermini

# Test silent operation
make test-silent PORT=/dev/ttyUSB0
```

### 4. Full Validation (Everything)
```bash
# Step 1: Compile all boards
make test

# Step 2: Run unit tests on hardware
make test-board BOARD=test-esp32-c3

# Step 3: Test silent mode
make test-silent PORT=/dev/ttyUSB0

# Step 4: Test with mock RotorHazard server
make test-rotorhazard PORT=/dev/ttyUSB0 BOARD=esp32dev
```

## What the Silent Mode Test Actually Checks

### Unit Test (`test_silent_operation.cpp`)
Checks software design:
- ✅ Config functions documented as silent
- ✅ Protocol message format correct
- ✅ Boot sequence architecture valid

### Hardware Integration Test (`test_silent_rotorhazard.py`)
Checks actual runtime behavior:
- ✅ No "Board:", "Mode:", "ConfigLoader:" text
- ✅ No ASCII debug messages
- ✅ Only binary protocol responses
- ✅ Silent during idle
- ✅ Silent during rapid commands

## CI/CD Integration

GitHub Actions runs:
```yaml
- name: Build Tests
  run: make test  # Compiles silent mode test ✅
```

**Note:** The actual silent mode hardware test requires:
- Physical device connected
- Serial port access
- Cannot run in GitHub Actions without self-hosted runner

## Summary Table

| Test Type | Command | Speed | Hardware? | Tests Silent Mode? |
|-----------|---------|-------|-----------|-------------------|
| **Compile Only** | `make test` | ⚡ Fast | ❌ No | Partially (compile) |
| **Unit Tests** | `make test-board` | 🏃 Medium | ✅ Yes | Yes (logic) |
| **Integration** | `make test-silent` | 🐌 Slow | ✅ Yes | **Yes (actual)** |

## Recommendation

For the silent mode feature specifically:

1. **Development:** `make test` (compiles, verifies syntax)
2. **Testing:** `make test-board BOARD=test-esp32-c3` (runs unit tests)
3. **Critical Validation:** `make test-silent PORT=/dev/ttyUSB0` (real test)

**Best practice:** Run `make test-silent` before every release to ensure no `Serial.print()` statements snuck in!

