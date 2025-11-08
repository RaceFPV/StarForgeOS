# StarForgeOS Test Checklist

Use this checklist when making changes to StarForgeOS to ensure compatibility across all board types.

## Pre-Commit Checklist

- [ ] Code compiles without warnings
- [ ] All board types build successfully (`./run_tests.sh build-only`)
- [ ] Tested on at least one physical board
- [ ] No new linter errors introduced
- [ ] Updated relevant documentation

## Board-Specific Features Checklist

### Core Functionality (All Boards)
- [ ] RSSI reading works correctly
- [ ] RX5808 frequency control responds
- [ ] Lap detection triggers properly
- [ ] Serial communication stable
- [ ] WiFi AP starts correctly
- [ ] Web interface accessible
- [ ] mDNS resolution works (`sfos.local`)

### Single-Core Boards (ESP32-C3, ESP32-C6, ESP32-S2)
- [ ] WiFi initializes before timing task
- [ ] No watchdog timeouts during operation
- [ ] Serial communication not blocked by timing task
- [ ] Mode switching works smoothly

### Dual-Core Boards (ESP32, ESP32-S3)
- [ ] Timing and web server run concurrently
- [ ] No core affinity issues
- [ ] Both cores utilized efficiently

### LCD Boards (ESP32-S3-Touch, JC2432W328C)
- [ ] Display initializes correctly
- [ ] Touch input responsive
- [ ] Backlight control works
- [ ] Mode switch button functional
- [ ] Battery percentage displays (if applicable)
- [ ] Audio beeps on laps (ESP32 only)

### Board-Specific Pin Configurations
- [ ] **ESP32-C3**: GPIO3 (ADC), GPIO6 (DATA), GPIO4 (CLK), GPIO7 (SEL)
- [ ] **ESP32-C6**: GPIO0 (ADC), GPIO6 (DATA), GPIO4 (CLK), GPIO7 (SEL)
- [ ] **ESP32**: GPIO34 (ADC), GPIO23 (DATA), GPIO18 (CLK), GPIO5 (SEL)
- [ ] **ESP32-S3-Touch**: GPIO4 (ADC), I2C on GPIO47/48, Backlight GPIO1
- [ ] **JC2432W328C**: GPIO35 (ADC), I2C on GPIO32/33, Backlight GPIO27

## Test Coverage Verification

### Hardware Tests
- [ ] `test_board_identification` passes
- [ ] `test_rssi_pin_defined` passes
- [ ] `test_rx5808_pins_defined` passes
- [ ] `test_mode_switch_pin` passes (non-LCD boards)
- [ ] `test_timing_constants` passes
- [ ] `test_freertos_priorities` passes
- [ ] `test_dma_adc_config` passes
- [ ] `test_wifi_config` passes
- [ ] `test_frequency_range` passes

### Timing Core Tests
- [ ] `test_timing_core_init` passes
- [ ] `test_threshold_set_get` passes
- [ ] `test_frequency_set_get` passes
- [ ] `test_activation_state` passes
- [ ] `test_rssi_reading` passes
- [ ] `test_peak_rssi_tracking` passes
- [ ] `test_state_reset` passes
- [ ] `test_concurrent_state_access` passes

### WiFi Tests
- [ ] `test_wifi_ap_init` passes
- [ ] `test_wifi_ap_ip` passes
- [ ] `test_wifi_ssid_generation` passes
- [ ] `test_wifi_mode_switching` passes
- [ ] `test_wifi_stability` passes

### LCD Tests (LCD Boards Only)
- [ ] `test_lcd_ui_enabled` passes
- [ ] `test_i2c_init` passes
- [ ] `test_i2c_bus_scan` passes
- [ ] `test_backlight_control` passes
- [ ] `test_touch_i2c_address` passes
- [ ] `test_battery_adc_pin` passes (if enabled)
- [ ] `test_board_specific_pins` passes

## Common Issues to Check

### Compilation Issues
- [ ] No undefined references to board-specific pins
- [ ] No conflicts between DMA ADC and analogRead()
- [ ] TFT_eSPI configured correctly for LCD boards
- [ ] LVGL configuration valid (LCD boards)

### Runtime Issues
- [ ] No watchdog resets during normal operation
- [ ] WiFi doesn't fail to start on single-core chips
- [ ] Serial output clean (no bootloader garbage)
- [ ] Timing task doesn't starve other tasks

### Memory Issues
- [ ] No heap fragmentation warnings
- [ ] Stack sizes adequate for all tasks
- [ ] PSRAM used efficiently (S3 boards)
- [ ] No memory leaks detected

## Manual Testing Procedures

### Basic Functionality Test (5 minutes)
1. Upload firmware to board
2. Verify serial output shows correct board type
3. Connect to WiFi AP (SFOS_XXXX)
4. Access web interface at 192.168.4.1
5. Set frequency to 5800 MHz
6. Verify RSSI reading updates
7. Start race, verify lap detection works

### WiFi Stability Test (10 minutes)
1. Upload firmware
2. Connect to WiFi AP
3. Access web interface
4. Leave connection open for 10 minutes
5. Verify no disconnections
6. Verify web interface still responsive

### Timing Accuracy Test (5 minutes)
1. Upload firmware
2. Connect RX5808 module
3. Set frequency to match video transmitter
4. Enable timing
5. Pass drone in front of receiver
6. Verify lap is detected
7. Check RSSI peak value is reasonable

### LCD Test (LCD Boards, 5 minutes)
1. Upload firmware
2. Verify display shows UI
3. Test touch responsiveness
4. Toggle backlight
5. Check battery percentage (if applicable)
6. Test mode switch button
7. Verify lap display updates

## Regression Testing

When fixing a bug or adding a feature, verify:

- [ ] Original issue is resolved
- [ ] No new issues introduced
- [ ] All board types still work
- [ ] Performance not degraded
- [ ] Documentation updated

## Release Checklist

Before tagging a new release:

- [ ] All tests pass on CI
- [ ] Manual testing completed on at least 3 board types
- [ ] Release notes written
- [ ] Version number updated
- [ ] Firmware binaries built for all boards
- [ ] Documentation updated
- [ ] Breaking changes documented

## CI Pipeline Status

Check that GitHub Actions workflow passes:

- [ ] ESP32-C3 build successful
- [ ] ESP32-C6 build successful
- [ ] ESP32 build successful
- [ ] ESP32-S2 build successful
- [ ] ESP32-S3 build successful
- [ ] ESP32-S3-Touch build successful
- [ ] JC2432W328C build successful
- [ ] Firmware artifacts uploaded

## Notes

### ESP32-C3 Specific
- Remember: WiFi.softAP() must be called before high-priority tasks start
- Single-core means timing task can starve other operations
- USB CDC baud rate is ignored (hardware USB)

### ESP32-S3 Specific
- No built-in DAC, audio must be disabled
- PSRAM available for large buffers
- USB CDC for serial (like C3)

### LCD Boards
- Mode selection via touchscreen, not physical pin
- Battery monitoring may conflict with DMA ADC
- Touch I2C address typically 0x15

## Additional Resources

- Full test documentation: `test/README.md`
- Quick start guide: `test/QUICKSTART.md`
- Hardware docs: `docs/hardware.md`
- Pin configurations: `src/config.h`

---

**Last Updated**: 2024
**Tested Board Types**: 7
**Test Categories**: 4
**Total Test Cases**: 40+

