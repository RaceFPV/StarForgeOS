# Silent Mode Testing for RotorHazard

## Critical Requirement

**RotorHazard mode MUST NOT produce any Serial debug output!**

The RotorHazard server communicates with nodes using a binary serial protocol. Any stray `Serial.print()` or `Serial.println()` statements will:
- ❌ Break the protocol communication
- ❌ Cause parsing errors on the server
- ❌ Result in node disconnection
- ❌ Make the node unusable in RotorHazard

## What Must Be Silent

### Configuration Loading (BEFORE mode detection)
```cpp
// ❌ WRONG - prints before we know the mode
String ConfigLoader::loadDefaultMode() {
    Serial.println("Loading mode...");  // BREAKS ROTORHAZARD!
    return "standalone";
}

// ✅ CORRECT - completely silent
String ConfigLoader::loadDefaultMode() {
    // No Serial output
    return "standalone";
}
```

### Mode Detection
```cpp
// ❌ WRONG - prints during mode detection
pinMode(g_mode_switch_pin, INPUT_PULLUP);
bool state = digitalRead(g_mode_switch_pin);
Serial.printf("Mode pin: %d\n", state);  // BREAKS ROTORHAZARD!

// ✅ CORRECT - silent until mode is known
pinMode(g_mode_switch_pin, INPUT_PULLUP);
bool state = digitalRead(g_mode_switch_pin);
current_mode = (state == LOW) ? MODE_STANDALONE : MODE_ROTORHAZARD;

// ONLY print if standalone
if (current_mode == MODE_STANDALONE) {
    Serial.printf("Mode pin: %d\n", state);  // Safe!
}
```

### Runtime Operations
```cpp
// ❌ WRONG - always prints
void handlePowerButton() {
    Serial.println("Button pressed");  // BREAKS ROTORHAZARD!
}

// ✅ CORRECT - guarded by mode check
void handlePowerButton() {
    if (current_mode == MODE_STANDALONE) {
        Serial.println("Button pressed");  // Safe!
    }
}
```

## Testing for Silent Operation

### 1. Unit Tests (No Hardware Required)

Run the silent operation unit tests:

```bash
cd StarForgeOS/test
pio test -e test-esp32-c3 -f test_silent
```

These tests verify:
- ✅ ConfigLoader functions are silent
- ✅ No text output in protocol paths
- ✅ Boot sequence design is correct

### 2. Hardware Integration Test

Test with real hardware using the Python script:

```bash
cd StarForgeOS/test/tools

# Flash the device in RotorHazard mode (default)
# Make sure mode switch pin is FLOATING

# Run the silent mode test
python3 test_silent_rotorhazard.py /dev/ttyUSB0
```

**What the test does:**
1. Connects to the node
2. Waits for boot to complete
3. Sends protocol commands
4. Checks that ONLY protocol responses are received
5. Verifies no debug text appears

**Expected output:**
```
============================================================
StarForgeOS RotorHazard Silent Mode Test
============================================================
✓ Connected to /dev/ttyUSB0
⏳ Waiting for boot to complete...
✓ Boot complete, buffer cleared

[TEST] READ_ADDRESS (should return 1 byte, no debug)
✓ Got API level: 35 (correct)

[TEST] READ_FREQUENCY (should return 2 bytes, no debug)
✓ Got frequency: 5800 MHz (correct)

[TEST] WRITE_FREQUENCY (should be silent, no response)
✓ No output (correct - write commands don't respond)

[TEST] Idle Silence (no commands, should be silent)
✓ No idle output (correct)

[TEST] Rapid Commands (should stay silent)
✓ Got 5 bytes (expected ~5)

============================================================
✅ ALL TESTS PASSED - RotorHazard mode is SILENT
============================================================
```

### 3. Full Integration Test with Mock Server

Test with the mock RotorHazard server:

```bash
cd StarForgeOS/test/tools

# Run the mock server
python3 mock_rotorhazard.py /dev/ttyUSB0
```

This simulates a real RotorHazard server and will fail if any debug output is detected.

## Common Violations and Fixes

### Violation 1: Config Loader Prints

```cpp
// ❌ WRONG
bool ConfigLoader::loadCustomConfig(CustomPinConfig* config) {
    Serial.println("Loading config...");
    // ...
}

// ✅ CORRECT
bool ConfigLoader::loadCustomConfig(CustomPinConfig* config) {
    // IMPORTANT: No Serial output - might be in RotorHazard mode
    // ...
}
```

### Violation 2: Unconditional Debug Messages

```cpp
// ❌ WRONG
void setup() {
    Serial.println("StarForge Timer v1.0");  // Always prints
    current_mode = detectMode();
}

// ✅ CORRECT
void setup() {
    current_mode = detectMode();  // Detect FIRST
    
    if (current_mode == MODE_STANDALONE) {
        Serial.println("StarForge Timer v1.0");  // Only print if safe
    }
}
```

### Violation 3: Runtime Event Messages

```cpp
// ❌ WRONG
void onButtonPress() {
    Serial.println("Button pressed");  // Always prints
    handleButton();
}

// ✅ CORRECT
void onButtonPress() {
    if (current_mode == MODE_STANDALONE) {
        Serial.println("Button pressed");  // Guarded
    }
    handleButton();
}
```

## Debugging RotorHazard Mode

**Problem:** How do you debug if you can't use Serial.print?

### Safe Debugging Methods:

#### 1. LED Blinks
```cpp
// Use LED patterns to indicate state
pinMode(LED_PIN, OUTPUT);
for (int i = 0; i < lap_count; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(200);
    digitalWrite(LED_PIN, LOW);
    delay(200);
}
```

#### 2. SPIFFS Logging
```cpp
void debugLog(const char* message) {
    if (current_mode == MODE_ROTORHAZARD) {
        File log = SPIFFS.open("/debug.log", "a");
        log.println(message);
        log.close();
    } else {
        Serial.println(message);
    }
}
```

#### 3. Test with Mock Server First
```bash
# Always test with mock server before real hardware
python3 mock_rotorhazard.py /dev/ttyUSB0
```

#### 4. Temporarily Switch Modes
```cpp
// In config.json, set default_mode to "standalone" for debugging
{
  "default_mode": "standalone"
}
// Then reflash and test, switch back to "rotorhazard" for production
```

## Checklist for New Code

When adding new features, ensure:

- [ ] No Serial.print() before mode detection
- [ ] All Serial.print() guarded by `if (current_mode == MODE_STANDALONE)`
- [ ] Config loading functions are silent
- [ ] Protocol handlers only send binary responses
- [ ] Event handlers check mode before printing
- [ ] Run `test_silent_operation` unit tests
- [ ] Run `test_silent_rotorhazard.py` with hardware
- [ ] Test with `mock_rotorhazard.py`

## Why This Matters

**Real-world impact:**

Without silent mode:
```
RotorHazard Server Log:
Node 1: Connected
Node 1: ConfigLoader: Loading pins...  ← PROTOCOL ERROR
Node 1: Board: ESP32-C3                ← PROTOCOL ERROR
Node 1: Disconnected (parse error)
```

With silent mode:
```
RotorHazard Server Log:
Node 1: Connected
Node 1: API Level 35
Node 1: Frequency 5800 MHz
Node 1: Ready ✓
```

## Integration with CI/CD

The silent mode test can be added to your CI pipeline:

```yaml
# .github/workflows/test.yml
- name: Test Silent RotorHazard Mode
  run: |
    cd StarForgeOS/test
    pio test -e test-esp32-c3 -f test_protocol/test_silent_operation
```

## References

- [RotorHazard Protocol Specification](https://github.com/RotorHazard/RotorHazard/blob/main/doc/USB%20Nodes.md)
- [Node Mode Implementation](../../src/node_mode.cpp)
- [Main Mode Detection](../../src/main.cpp)
- [Config Loader](../../src/config_loader.cpp)

## Support

If silent mode tests fail:
1. Review the violations list from the test output
2. Check the code locations mentioned
3. Add `if (current_mode == MODE_STANDALONE)` guards
4. Re-run tests to verify fix
5. Test with mock_rotorhazard.py before hardware

**Remember:** When in doubt, stay silent! 🤫

