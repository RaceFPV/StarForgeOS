#ifndef NODE_MODE_H
#define NODE_MODE_H

#include <Arduino.h>
#include "timing_core.h" // To interact with timing data

// Node settings structure (matching original node code)
struct NodeSettings {
    uint16_t vtxFreq = 5800;
    uint8_t enterAtLevel = 96;
    uint8_t exitAtLevel = 80;
};

// Last pass data structure (matching original node code)
struct NodeLastPass {
    uint32_t timestamp = 0;
    uint8_t rssiPeak = 0;
    uint8_t lap = 0;
};

// Forward declaration for friend class
struct Message;

class NodeMode {
public:
    NodeMode();
    void begin(TimingCore* timingCore);
    void process();
    void handleSerialInput();
    
    friend struct Message;  // Allow Message to access private members

private:
    TimingCore* _timingCore;
    NodeSettings _settings;
    NodeLastPass _lastPass;
    uint8_t _nodeIndex = 0;
    uint8_t _slotIndex = 0;

    void sendLapDataToHost(const LapData& lap);
};

#endif // NODE_MODE_H