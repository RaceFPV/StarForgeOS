# Silent Mode Testing - Quick Reference

## The Problem

**RotorHazard uses a binary serial protocol.** Any text output (like `Serial.println()`) breaks communication!

```cpp
// ❌ THIS BREAKS ROTORHAZARD:
void setup() {
    Serial.println("Booting...");  // Text output!
    current_mode = detectMode();
}

// ✅ THIS WORKS:
void setup() {
    current_mode = detectMode();
    if (current_mode == MODE_STANDALONE) {
        Serial.println("Booting...");  // Only print if safe!
    }
}
```

## Quick Test (2 Steps)

### 1. Unit Test (No Hardware)

```bash
cd StarForgeOS/test
pio test -e test-esp32-c3 -f test_silent
```

**Tests:**
- ✅ Config loading is silent
- ✅ No text in protocol paths
- ✅ Boot sequence design

### 2. Hardware Test (With Device)

```bash
cd StarForgeOS/test
./run_tests_simple.sh silent /dev/ttyUSB0
```

**Tests:**
- ✅ No debug output on boot
- ✅ Protocol commands work
- ✅ No idle chatter
- ✅ Rapid commands don't trigger prints

## Expected Output

```
============================================================
StarForgeOS RotorHazard Silent Mode Test
============================================================
✓ Connected to /dev/ttyUSB0
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

## If Tests Fail

You'll see violations like:

```
✗ Found debug text 'Board:' in READ_ADDRESS response
✗ Found debug text 'ConfigLoader:' in idle period
✗ High ASCII content in READ_FREQUENCY response
```

**Fix:** Add mode guards:

```cpp
// Find the offending Serial.print() and guard it:
if (current_mode == MODE_STANDALONE) {
    Serial.println("Your debug message");
}
```

## The Rules

1. **NEVER** `Serial.print()` before mode is determined
2. **ALWAYS** guard prints with `if (current_mode == MODE_STANDALONE)`
3. **CONFIG LOADING MUST BE SILENT** (happens before mode detection)
4. **PROTOCOL HANDLERS** only send binary responses

## Safe Alternatives for Debugging RotorHazard

When you need to debug RotorHazard mode:

### 1. LED Blinks 💡
```cpp
for (int i = 0; i < error_code; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(200);
    digitalWrite(LED_PIN, LOW);
    delay(200);
}
```

### 2. SPIFFS Logging 📝
```cpp
File log = SPIFFS.open("/debug.log", "a");
log.printf("Error code: %d\n", code);
log.close();
```

### 3. Test with Mock Server First 🧪
```bash
python3 tools/mock_rotorhazard.py /dev/ttyUSB0
```

### 4. Temporarily Switch to Standalone 🔄
Flash with `default_mode: "standalone"` in config, debug, then switch back.

## More Details

- Full guide: [SILENT_MODE_TESTING.md](SILENT_MODE_TESTING.md)
- Protocol tests: [test_protocol/](test_protocol/)
- Mock server: [tools/mock_rotorhazard.py](tools/mock_rotorhazard.py)
- Hardware test: [tools/test_silent_rotorhazard.py](tools/test_silent_rotorhazard.py)

## Remember

**When in doubt, stay silent!** 🤫

It's better to have no debug output than to break RotorHazard communication.

