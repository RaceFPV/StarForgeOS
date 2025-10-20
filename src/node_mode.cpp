#include "node_mode.h"
#include "config.h"

// Adapted from the original rhnode.cpp and commands.cpp
// This implements the RotorHazard node protocol over serial

extern const char *firmwareVersionString;
extern const char *firmwareBuildDateString;
extern const char *firmwareBuildTimeString;
extern const char *firmwareProcTypeString;

// Message buffer for serial communication (will be defined later)

// Settings flags (from commands.cpp)
uint8_t settingChangedFlags = 0;

// Command constants (from RHInterface.py - MUST match server exactly)
#define READ_ADDRESS 0x00
#define READ_FREQUENCY 0x03
#define READ_LAP_STATS 0x05
#define READ_LAP_PASS_STATS 0x0D
#define READ_LAP_EXTREMUMS 0x0E
#define READ_RHFEAT_FLAGS 0x11
#define READ_REVISION_CODE 0x22
#define READ_NODE_RSSI_PEAK 0x23
#define READ_NODE_RSSI_NADIR 0x24
#define READ_ENTER_AT_LEVEL 0x31
#define READ_EXIT_AT_LEVEL 0x32
#define READ_TIME_MILLIS 0x33
#define READ_MULTINODE_COUNT 0x39
#define READ_CURNODE_INDEX 0x3A
#define READ_NODE_SLOTIDX 0x3C
#define READ_FW_VERSION 0x3D
#define READ_FW_BUILDDATE 0x3E
#define READ_FW_BUILDTIME 0x3F
#define READ_FW_PROCTYPE 0x40

#define WRITE_FREQUENCY 0x51
#define WRITE_ENTER_AT_LEVEL 0x71
#define WRITE_EXIT_AT_LEVEL 0x72
#define SEND_STATUS_MESSAGE 0x75
#define FORCE_END_CROSSING 0x78
#define WRITE_CURNODE_INDEX 0x7A
#define JUMP_TO_BOOTLOADER 0x7E

#define NODE_API_LEVEL 35

// #define NODE_API_LEVEL 42  // Removed duplicate definition
#define RHFEAT_FLAGS_VALUE 0x0000  // No special features for ESP32 Lite

// Status flags
#define COMM_ACTIVITY 0x01
#define SERIAL_CMD_MSG 0x02
#define FREQ_SET 0x04
#define FREQ_CHANGED 0x08
#define ENTERAT_CHANGED 0x10
#define EXITAT_CHANGED 0x20
#define LAPSTATS_READ 0x40

// Message buffer implementation (adapted from io.h)
struct Buffer {
    uint8_t data[32];
    uint8_t size = 0;
    uint8_t index = 0;
    
    bool isEmpty() { return size == 0; }
    void flipForRead() { index = 0; }
    void flipForWrite() { size = 0; }
    
    uint8_t read8() { return data[index++]; }
    uint16_t read16() {
        uint16_t result = data[index++] << 8;
        result |= data[index++];
        return result;
    }
    uint32_t read32() {
        uint32_t result = data[index++] << 24;
        result |= data[index++] << 16;
        result |= data[index++] << 8;
        result |= data[index++];
        return result;
    }
    
    void write8(uint8_t v) { data[size++] = v; }
    void write16(uint16_t v) {
        data[size++] = (v >> 8);
        data[size++] = v;
    }
    void write32(uint32_t v) {
        data[size++] = (v >> 24);
        data[size++] = (v >> 16);
        data[size++] = (v >> 8);
        data[size++] = v;
    }
    
    uint8_t calculateChecksum(uint8_t len) {
        uint8_t checksum = 0;
        for (uint8_t i = 0; i < len; i++) {
            checksum += data[i];  // SUM, not XOR (RotorHazard protocol uses sum)
        }
        return checksum & 0xFF;
    }
    
    void writeChecksum() {
        data[size] = calculateChecksum(size);
        size++;
    }
};

struct Message {
    uint8_t command = 0;
    Buffer buffer;
    NodeMode* nodeMode = nullptr;  // Pointer to NodeMode instance for accessing timing data
    
    byte getPayloadSize() {
        switch (command) {
            case WRITE_FREQUENCY: return 2;
            case WRITE_ENTER_AT_LEVEL: return 1;
            case WRITE_EXIT_AT_LEVEL: return 1;
            case SEND_STATUS_MESSAGE: return 2;
            case FORCE_END_CROSSING: return 1;
            case WRITE_CURNODE_INDEX: return 1;
            default: return 0;
        }
    }
    
    void handleWriteCommand(bool serialFlag);
    void handleReadCommand(bool serialFlag);
};

// Message buffer for serial communication
Message serialMessage;

NodeMode::NodeMode() : _timingCore(nullptr) {
}

void NodeMode::begin(TimingCore* timingCore) {
    _timingCore = timingCore;
    serialMessage.nodeMode = this;  // Set pointer for Message to access NodeMode data
    
    // Initialize with default settings ONLY if not already set
    // This prevents resetting frequency when mode is re-initialized
    static bool first_init = true;
    if (first_init) {
        _settings.vtxFreq = 5800;
        _settings.enterAtLevel = 96;
        _settings.exitAtLevel = 80;
        _nodeIndex = 0;
        _slotIndex = 0;
        first_init = false;
        
        // Set default frequency and threshold ONLY on first initialization
        if (_timingCore) {
            _timingCore->setFrequency(_settings.vtxFreq);
            _timingCore->setThreshold(_settings.enterAtLevel);
        }
    }
    
    // Always ensure timing core is activated for node mode
    if (_timingCore) {
        _timingCore->setActivated(true);
    }
    
    // Debug output disabled to avoid interfering with serial protocol
    // Serial.println("RotorHazard Node Mode initialized");
    // Serial.printf("Firmware: %s\n", firmwareVersionString);
    // Serial.printf("Build: %s %s\n", firmwareBuildDateString, firmwareBuildTimeString);
    // Serial.printf("Processor: %s\n", firmwareProcTypeString);
}

void NodeMode::process() {
    // Handle incoming serial data
    handleSerialInput();
    
    // Check for new lap data from timing core
    if (_timingCore && _timingCore->hasNewLap()) {
        LapData lap = _timingCore->getNextLap();
        // Convert to RotorHazard format and update internal state
        _lastPass.timestamp = lap.timestamp_ms;
        _lastPass.rssiPeak = lap.rssi_peak;
        _lastPass.lap++;  // Increment lap counter
        
        // Debug output disabled to avoid interfering with serial protocol
        // Serial.printf("Lap %d detected: %dms, RSSI: %d\n", 
        //              _lastPass.lap, lap.timestamp_ms, lap.rssi_peak);
    }
}

void NodeMode::handleSerialInput() {
    int iterCount = 0;
    // Process all available bytes (USB CDC can have multiple bytes queued)
    while (Serial.available() > 0) {
        uint8_t nextByte = Serial.read();
        
        if (serialMessage.buffer.size == 0) {
            // New command
            serialMessage.command = nextByte;
            if (serialMessage.command > 0x50) {
                // Write command
                byte expectedSize = serialMessage.getPayloadSize();
                if (expectedSize > 0) {
                    serialMessage.buffer.index = 0;
                    serialMessage.buffer.size = expectedSize + 1;  // include checksum
                }
            } else {
                // Read command - handle immediately
                serialMessage.handleReadCommand(true);
                
                if (serialMessage.buffer.size > 0) {
                    // Send response
                    Serial.write((byte *)serialMessage.buffer.data, serialMessage.buffer.size);
                    Serial.flush();  // Ensure data is sent immediately
                    serialMessage.buffer.size = 0;
                }
            }
        } else {
            // Existing command - collect payload data
            serialMessage.buffer.data[serialMessage.buffer.index++] = nextByte;
            if (serialMessage.buffer.index == serialMessage.buffer.size) {
                // Complete message received, verify checksum
                uint8_t checksum = serialMessage.buffer.calculateChecksum(serialMessage.buffer.size - 1);
                if (serialMessage.buffer.data[serialMessage.buffer.size - 1] == checksum) {
                    serialMessage.handleWriteCommand(true);
                } else {
                    // Debug output disabled to avoid interfering with serial protocol
                // Serial.println("Checksum error");
                }
                serialMessage.buffer.size = 0;
            }
        }
        
        // Increased iteration limit for better serial responsiveness
        if (++iterCount > 100) return;  // Prevent blocking (was 20, now 100)
    }
}

// Command handling implementation (adapted from commands.cpp)
void Message::handleWriteCommand(bool serialFlag) {
    buffer.flipForRead();
    bool actFlag = true;
    
    // Access NodeMode instance through global pointer or singleton pattern
    // For now, we'll handle basic commands
    
    switch (command) {
        case WRITE_FREQUENCY: {
            uint16_t freq = buffer.read16();
            // Set frequency via timing core AND update settings
            if (nodeMode && nodeMode->_timingCore) {
                nodeMode->_settings.vtxFreq = freq;  // Update stored settings
                nodeMode->_timingCore->setFrequency(freq);
                nodeMode->_timingCore->setActivated(true);  // Activate node after frequency is set
                // Reset peak tracking when frequency changes (same as RotorHazard)
                TimingState state = nodeMode->_timingCore->getState();
                state.peak_rssi = 0;
            }
            settingChangedFlags |= FREQ_SET | FREQ_CHANGED;
            break;
        }
        
        case WRITE_ENTER_AT_LEVEL: {
            uint8_t level = buffer.read8();
            // Set enter threshold via timing core AND update settings
            if (nodeMode && nodeMode->_timingCore) {
                nodeMode->_settings.enterAtLevel = level;  // Update stored settings
                nodeMode->_timingCore->setThreshold(level);
            }
            settingChangedFlags |= ENTERAT_CHANGED;
            break;
        }
        
        case WRITE_EXIT_AT_LEVEL: {
            uint8_t level = buffer.read8();
            // Set exit threshold via timing core (same as enter for now) AND update settings
            if (nodeMode && nodeMode->_timingCore) {
                nodeMode->_settings.exitAtLevel = level;  // Update stored settings
                nodeMode->_timingCore->setThreshold(level);
            }
            settingChangedFlags |= EXITAT_CHANGED;
            break;
        }
        
        case FORCE_END_CROSSING: {
            // Debug output disabled to avoid interfering with serial protocol
            // Serial.println("Force end crossing");
            break;
        }
        
        case SEND_STATUS_MESSAGE: {
            uint16_t msg = buffer.read16();
            // Debug output disabled to avoid interfering with serial protocol
            // Serial.printf("Status message: 0x%04X\n", msg);
            break;
        }
        
        default:
            // Debug output disabled to avoid interfering with serial protocol
            // Serial.printf("Unknown write command: 0x%02X\n", command);
            actFlag = false;
    }
    
    if (actFlag) {
        settingChangedFlags |= COMM_ACTIVITY;
        if (serialFlag) {
            settingChangedFlags |= SERIAL_CMD_MSG;
        }
    }
    
    command = 0;  // Clear command
}

void Message::handleReadCommand(bool serialFlag) {
    buffer.flipForWrite();
    bool actFlag = true;
    
    switch (command) {
        case READ_ADDRESS:
            buffer.write8(0x08);  // Default node address
            break;
            
        case READ_FREQUENCY:
            // Return actual frequency from timing core
            if (nodeMode && nodeMode->_timingCore) {
                TimingState state = nodeMode->_timingCore->getState();
                buffer.write16(state.frequency_mhz);
            } else {
                buffer.write16(5800);  // Default frequency if timing core not available
            }
            break;
            
        case READ_LAP_PASS_STATS: {
            uint32_t timeNow = millis();
            uint8_t current_rssi = 0;
            uint8_t peak_rssi = 0;
            uint8_t lap_num = 0;
            uint16_t ms_since_lap = 0;
            uint8_t lap_peak = 0;
            
            if (nodeMode && nodeMode->_timingCore) {
                current_rssi = nodeMode->_timingCore->getCurrentRSSI();
                peak_rssi = nodeMode->_timingCore->getPeakRSSI();
                lap_num = nodeMode->_lastPass.lap;
                ms_since_lap = timeNow - nodeMode->_lastPass.timestamp;
                lap_peak = nodeMode->_lastPass.rssiPeak;
            }
            
            buffer.write8(lap_num);  // lap number
            buffer.write16(ms_since_lap);  // ms since lap
            buffer.write8(current_rssi);  // current RSSI
            buffer.write8(peak_rssi); // node RSSI peak
            buffer.write8(lap_peak); // lap RSSI peak
            buffer.write16(1000); // loop time micros (placeholder)
            settingChangedFlags |= LAPSTATS_READ;
            break;
        }
        
        case READ_LAP_EXTREMUMS: {
            buffer.write8(0);  // flags
            buffer.write8(30); // rssi nadir since last pass
            buffer.write8(30); // node rssi nadir
            // Extremum data (rssi, time offset, duration)
            buffer.write8(0);
            buffer.write16(0);
            buffer.write16(0);
            break;
        }
        
        case READ_ENTER_AT_LEVEL:
            // Return actual threshold from timing core
            if (nodeMode && nodeMode->_timingCore) {
                TimingState state = nodeMode->_timingCore->getState();
                buffer.write8(state.threshold);
            } else {
                buffer.write8(96);  // Default threshold
            }
            break;
            
        case READ_EXIT_AT_LEVEL:
            // Return actual threshold from timing core (same as enter for now)
            if (nodeMode && nodeMode->_timingCore) {
                TimingState state = nodeMode->_timingCore->getState();
                buffer.write8(state.threshold);
            } else {
                buffer.write8(80);  // Default threshold
            }
            break;
            
        case READ_REVISION_CODE:
            buffer.write16((0x25 << 8) + NODE_API_LEVEL);
            break;
            
        case READ_NODE_RSSI_PEAK:
            buffer.write8((nodeMode && nodeMode->_timingCore) ? nodeMode->_timingCore->getPeakRSSI() : 0);
            break;
            
        case READ_NODE_RSSI_NADIR:
            buffer.write8(30);  // TODO: Implement nadir tracking
            break;
            
        case READ_TIME_MILLIS:
            buffer.write32(millis());
            break;
            
        case READ_RHFEAT_FLAGS:
            buffer.write16(RHFEAT_FLAGS_VALUE);
            break;
            
        case READ_MULTINODE_COUNT:
            buffer.write8(1);
            break;
            
        case READ_CURNODE_INDEX:
            buffer.write8(0);
            break;
            
        case READ_NODE_SLOTIDX:
            buffer.write8(0);
            break;
            
        case READ_FW_VERSION:
            // Write firmware version string
            {
                const char* version = "ESP32_Lite_1.0.0";
                uint8_t len = strlen(version);
                buffer.write8(len);
                for (int i = 0; i < len; i++) {
                    buffer.write8(version[i]);
                }
            }
            break;
            
        case READ_FW_BUILDDATE:
            {
                const char* date = __DATE__;
                uint8_t len = strlen(date);
                buffer.write8(len);
                for (int i = 0; i < len; i++) {
                    buffer.write8(date[i]);
                }
            }
            break;
            
        case READ_FW_BUILDTIME:
            {
                const char* time = __TIME__;
                uint8_t len = strlen(time);
                buffer.write8(len);
                for (int i = 0; i < len; i++) {
                    buffer.write8(time[i]);
                }
            }
            break;
            
        case READ_FW_PROCTYPE:
            {
                const char* proctype = "ESP32-C3";
                uint8_t len = strlen(proctype);
                buffer.write8(len);
                for (int i = 0; i < len; i++) {
                    buffer.write8(proctype[i]);
                }
            }
            break;
            
        default:
            // Debug output temporarily enabled to troubleshoot communication
            actFlag = false;
    }
    
    if (actFlag) {
        settingChangedFlags |= COMM_ACTIVITY;
        if (serialFlag) {
            settingChangedFlags |= SERIAL_CMD_MSG;
        }
    }
    
    if (!buffer.isEmpty()) {
        buffer.writeChecksum();
    }
    
    command = 0;  // Clear command
}

// Firmware version strings are defined in main.cpp
