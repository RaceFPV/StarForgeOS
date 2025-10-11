# RX5808 Initialization - CRITICAL FIX

## 🚨 The Root Cause

**The RX5808 module was NEVER being properly initialized!**

This is likely the **PRIMARY cause** of all the RSSI reading issues reported by users.

## What Was Missing

### PhobosLT (Working Timer)
```cpp
void RX5808::init() {
    pinMode(pins...);
    digitalWrite(...);
    resetRxModule();      // ← SENDS RESET COMMAND
    setFrequency(POWER_DOWN_FREQ_MHZ);
}

void RX5808::resetRxModule() {
    // Write zeros to register 0xF (reset register)
    setupRxModule();      // ← CONFIGURES POWER
}

void RX5808::setupRxModule() {
    setRxModulePower(0b11010000110111110011);  // ← REGISTER 0xA
}
```

### Previous rotorhazard-lite (Broken)
```cpp
void TimingCore::setupRX5808() {
    pinMode(pins...);
    digitalWrite(...);
    delay(100);
    // ← THAT'S IT! NO RESET! NO CONFIGURATION!
}
```

## What This Means

Without proper initialization, the RX5808 could be:
1. **In power-down mode** from previous use
2. **Using default register values** that don't work well
3. **In an unknown/undefined state**
4. **Not actually receiving anything** or receiving at wrong frequency
5. **Outputting garbage RSSI values**

## The Fix

### Added Two Critical Functions

#### 1. resetRX5808Module()
Sends reset command to register 0xF with 20 bits of zeros:
- Wakes module from power-down
- Clears all registers
- Puts module in known state

#### 2. configureRX5808Power()
Sends power configuration to register 0xA:
- Value: `0b11010000110111110011` (from PhobosLT)
- Disables unused features
- Optimizes power consumption
- Configures module for lap timing

### Protocol Details

Both functions send data using the RX5808 SPI protocol:
```
[4 bits: Register Address] [1 bit: Write Flag] [20 bits: Data]
```

**Register 0xF (Reset):**
- Address: 0xF (0b1111)
- Write: 1
- Data: 20 zeros

**Register 0xA (Power):**
- Address: 0xA (0b1010)
- Write: 1
- Data: 0b11010000110111110011

## Why This Was Overlooked

The original code appeared to work because:
1. The RX5808 might have been in a good state by chance
2. Other issues (RSSI scaling) masked the initialization problem
3. No one compared with a known-working implementation

## Impact

This fix should result in:
- ✅ **RX5808 actually working** (might not have been before!)
- ✅ **Consistent RSSI readings** across all devices
- ✅ **Proper frequency tuning** behavior
- ✅ **Reliable lap detection**

## Testing

After this fix, you should see:
1. Debug output showing "Resetting RX5808 module..."
2. Debug output showing "Configuring RX5808 power..."
3. RSSI values that respond to signal strength changes
4. Stable operation across power cycles

## Technical References

- RX5808 datasheet register definitions
- PhobosLT proven implementation
- RotorHazard original node code
- Community feedback on initialization issues

---

**This is the most critical fix of all. Without proper initialization, nothing else matters!**

