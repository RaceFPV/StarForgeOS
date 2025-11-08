# RotorHazard Integration Testing

Testing StarForgeOS node mode communication with RotorHazard without requiring a full RotorHazard server.

## Overview

The `mock_rotorhazard.py` script implements a **lightweight mock** of the RotorHazard serial protocol. It validates that your node firmware correctly implements the protocol without needing to run the full Python RotorHazard server.

## What It Tests

✅ **Protocol Communication**
- API level handshake
- Read/Write frequency
- Read/Write thresholds (enter/exit levels)
- Firmware version strings
- RSSI reading

✅ **Command Validation**
- All READ commands respond correctly
- WRITE commands update state
- Checksum validation
- Response timing

❌ **Does NOT Test** (requires full RotorHazard)
- Lap timing algorithms (server-side)
- Race management
- Web interface
- Database storage

## Quick Start

### 1. Connect Hardware
```bash
# Connect your ESP32 via USB
# Find the serial port
ls /dev/ttyUSB* /dev/ttyACM*
```

### 2. Run Integration Test
```bash
# Using Makefile (recommended)
make test-rotorhazard PORT=/dev/ttyUSB0 BOARD=esp32-c3-supermini

# Or manually
./test/tools/test_rotorhazard_integration.sh /dev/ttyUSB0 esp32-c3-supermini
```

### 3. Review Results
```
============================================================
StarForgeOS Node Mode - Protocol Test Suite
============================================================

[TEST] Reading API Level...
  ✓ API Level: 35 (expected 35)

[TEST] Reading Firmware Info...
  ✓ Version: FIRMWARE_VERSION: ESP32_1.0.0
  ✓ Build Date: Dec  8 2024
  ✓ Processor: ESP32

[TEST] Reading Frequency...
  ✓ Frequency: 5800 MHz

[TEST] Writing Frequency (5740 MHz)...
  ✓ Frequency set to: 5740 MHz

[TEST] Reading Thresholds...
  ✓ Enter at: 96, Exit at: 80

[TEST] Reading RSSI...
  ✓ RSSI Peak: 42

============================================================
Test Summary
============================================================
✓ PASS  API Level                  35
✓ PASS  Firmware Info               FIRMWARE_VERSION: ESP32_1.0.0
✓ PASS  Read Frequency              5800
✓ PASS  Write Frequency             5740
✓ PASS  Write Frequency             5800
✓ PASS  Read Thresholds             (96, 80)
✓ PASS  RSSI Reading                42
============================================================
Total: 7 tests, 7 passed, 0 failed
============================================================
```

## Manual Testing

### Run Mock Server Standalone
```bash
# Build and upload firmware first
pio run -e esp32-c3-supermini -t upload

# Run mock server
python3 test/tools/mock_rotorhazard.py /dev/ttyUSB0
```

## Why This Approach?

### ✅ Advantages
- **Fast**: ~5-10 seconds vs minutes for full RotorHazard
- **Simple**: No database, no web server, no dependencies
- **Focused**: Tests only the node protocol
- **CI-Friendly**: Can run in automation (with hardware)

### ❌ Limitations
- Requires physical hardware (can't run in CI without it)
- Doesn't test full RotorHazard integration
- Doesn't validate lap timing algorithms

## CI/CD Integration

### Local Development
```bash
# Before committing changes to node_mode.cpp
make test-rotorhazard PORT=/dev/ttyUSB0 BOARD=esp32dev
```

### GitHub Actions (with self-hosted runner)
If you have a self-hosted runner with ESP32 hardware:

```yaml
- name: RotorHazard Integration Test
  if: runner.os == 'Linux' && runner.self-hosted
  run: |
    make test-rotorhazard PORT=/dev/ttyUSB0 BOARD=esp32dev
```

**Note**: This requires a self-hosted runner with ESP32 connected. Not suitable for GitHub-hosted runners.

## Protocol Reference

The mock implements these commands from the RotorHazard protocol:

| Command | Code | Description |
|---------|------|-------------|
| READ_ADDRESS | 0x00 | API level |
| READ_FREQUENCY | 0x03 | Current frequency |
| READ_ENTER_AT_LEVEL | 0x31 | Enter threshold |
| READ_EXIT_AT_LEVEL | 0x32 | Exit threshold |
| READ_NODE_RSSI_PEAK | 0x23 | Peak RSSI |
| READ_FW_VERSION | 0x3D | Firmware version |
| WRITE_FREQUENCY | 0x51 | Set frequency |
| WRITE_ENTER_AT_LEVEL | 0x71 | Set enter threshold |
| WRITE_EXIT_AT_LEVEL | 0x72 | Set exit threshold |

See `src/node_mode.cpp` for complete protocol implementation.

## Testing with Real RotorHazard

For comprehensive testing, use a real RotorHazard server:

1. **Install RotorHazard**
   ```bash
   # Clone and setup RotorHazard
   git clone https://github.com/RotorHazard/RotorHazard.git
   cd RotorHazard/src/server
   pip install -r requirements.txt
   ```

2. **Configure Node**
   - Flash StarForgeOS to ESP32
   - Connect via USB
   - Start RotorHazard server
   - Add node in web interface

3. **Validate**
   - Check node appears in web interface
   - Verify RSSI readings update
   - Test lap detection
   - Verify frequency changes work

## Troubleshooting

### "No response from node"
- Check serial port: `ls /dev/ttyUSB*`
- Verify baud rate (should be 921600)
- Ensure node is in RotorHazard mode (not WiFi mode)
- Try resetting the ESP32

### "API Level mismatch"
- Update `NODE_API_LEVEL` in `node_mode.cpp`
- Ensure you're using compatible RotorHazard version

### "Checksum errors"
- Baud rate mismatch (ESP32 boot messages can corrupt initial bytes)
- USB cable quality (try different cable)
- Wait longer after boot before testing

## Future Enhancements

- [ ] Automated lap detection testing
- [ ] Multi-node testing
- [ ] Protocol fuzzing for robustness
- [ ] Performance benchmarking
- [ ] Docker container for full RotorHazard testing

## Related Files

- `src/node_mode.cpp` - Node protocol implementation
- `src/node_mode.h` - Node mode interface
- `test/tools/mock_rotorhazard.py` - Mock server
- `test/tools/test_rotorhazard_integration.sh` - Test runner

---

**Need help?** See `test/README.md` for complete testing documentation.

