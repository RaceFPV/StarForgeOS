# Default Operation Mode Feature

## Overview
The flash tool now supports setting the default operation mode (Standalone or RotorHazard) that the device will start in on power-up.

## How It Works

### Flash Tool UI
In the **Advanced Configuration** section of the flash tool, you'll find a new dropdown:

```
Default Operation Mode
├── Standalone (WiFi Timer)    [Default]
└── RotorHazard (USB Node)
```

This setting is written to `/config.json` on the device's SPIFFS partition during flashing.

### Configuration File Format
The config.json file now includes a `default_mode` field:

```json
{
  "default_mode": "standalone",
  "custom_pins": {
    "enabled": false
  }
}
```

Valid values:
- `"standalone"` - Device starts in WiFi timer mode (default)
- `"rotorhazard"` - Device starts in RotorHazard USB node mode

### Device Behavior

#### LCD/Touch Boards (ESP32-S3-Touch, JC2432W328C)
- Device boots into the configured default mode
- User can switch modes via the LCD touchscreen UI
- Setting persists across reboots

#### Non-Touch Boards (Generic ESP32, ESP32-C3, ESP32-C6)
- **Mode Switch Pin LOW (grounded)**: Forces Standalone mode (overrides default)
- **Mode Switch Pin HIGH (floating)**: Uses configured default mode
- This provides hardware override capability while still respecting the default

### Code Implementation

**Files Modified:**
1. `flash_tool/index.html` - Added dropdown UI element
2. `flash_tool/renderer.js` - Updated config generation to include default_mode
3. `src/config_loader.h` - Added `loadDefaultMode()` function
4. `src/config_loader.cpp` - Implemented default mode loading from config.json
5. `src/main.cpp` - Updated startup logic to use configured default mode

## Usage Examples

### Example 1: Competition Timer (Standalone Default)
```
Default Mode: Standalone (WiFi Timer)
```
- Device boots into WiFi AP mode
- Ready for pilots to connect and race
- Can switch to RotorHazard mode via LCD if needed

### Example 2: RotorHazard Node (RotorHazard Default)
```
Default Mode: RotorHazard (USB Node)
```
- Device boots into USB node mode
- Immediately starts RotorHazard protocol
- Perfect for permanent timer installations
- Can switch to Standalone mode via LCD for testing

### Example 3: Development/Testing
```
Default Mode: Standalone
Mode Switch: Grounded → Forces Standalone mode
```
- Hardware override ensures you can always access WiFi/LCD
- Useful when developing/debugging RotorHazard mode

## Technical Details

### Startup Sequence
1. Device boots and loads `config.json` from SPIFFS
2. `ConfigLoader::loadDefaultMode()` reads the `default_mode` field
3. For LCD boards: Default mode is used directly
4. For non-touch boards: Mode switch pin can override to Standalone
5. Device initializes in the determined mode

### Default Fallback
If no config.json exists or default_mode is missing:
- **Default**: Standalone mode
- Ensures device is always accessible via WiFi on first boot

### Serial Output
The device prints the mode selection during boot:
```
ConfigLoader: Default mode loaded from config: standalone
Touch board detected: Mode switch via LCD UI
Default mode from config: STANDALONE
```

## Benefits

1. **User Convenience**: Set preference once during flashing
2. **Competition Ready**: Configure all nodes for RotorHazard mode before event
3. **Safety**: Hardware override ensures recovery from any mode
4. **Flexibility**: Easy to change via reflashing or LCD UI
5. **Persistence**: Setting survives reboots and firmware updates (if SPIFFS preserved)

## Backwards Compatibility

Devices without config.json or without the default_mode field will:
- Default to Standalone mode (existing behavior)
- Continue to work normally
- Show appropriate console messages

## Future Enhancements

Possible improvements:
- Web UI to change default mode without reflashing
- Remember last used mode across reboots
- Mode-specific boot animations on LCD
- API endpoint to query/change default mode

