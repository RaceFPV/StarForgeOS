#ifndef TIMING_CORE_H
#define TIMING_CORE_H

#include <Arduino.h>
#include <cstdint>
#include "config.h"

// Structure to hold lap data
struct LapData {
  uint32_t timestamp_ms;    // Lap timestamp in milliseconds
  uint16_t lap_time_ms;     // Time since last lap (0 for first lap)
  uint8_t rssi_peak;        // Peak RSSI during this lap
  uint8_t pilot_id;         // Pilot ID (0-based)
  bool valid;               // Whether this lap data is valid
};

// Structure to hold current timing state
struct TimingState {
  uint8_t current_rssi;     // Current filtered RSSI value
  uint8_t peak_rssi;        // Peak RSSI since last reset
  uint8_t threshold;        // Current crossing threshold
  bool crossing_active;     // Whether we're currently in a crossing
  uint32_t crossing_start;  // When current crossing started
  uint32_t last_lap_time;   // Timestamp of last lap
  uint16_t lap_count;       // Number of laps completed
  uint16_t frequency_mhz;   // Current RX frequency
  bool activated;           // Whether timing is active
};

class TimingCore {
private:
  TimingState state;
  LapData lap_buffer[MAX_LAPS_STORED];
  uint8_t lap_write_index;
  uint8_t lap_read_index;
  
  // RSSI filtering
  uint16_t rssi_samples[RSSI_SAMPLES];
  uint8_t sample_index;
  bool samples_filled;
  
  // FreeRTOS task handle for ESP32-C3 single core
  TaskHandle_t timing_task_handle;
  SemaphoreHandle_t timing_mutex;
  
  // Debug mode flag
  bool debug_enabled;
  
  // RX5808 frequency stability tracking
  bool recent_freq_change;
  uint32_t freq_change_time;
  
  // RX5808 control
  void setupRX5808();
  void resetRX5808Module();
  void configureRX5808Power();
  void setRX5808Frequency(uint16_t freq_mhz);
  void sendRX5808Bit(uint8_t bit_value);
  
  // RSSI processing
  uint8_t readRawRSSI();
  uint8_t filterRSSI(uint8_t raw_rssi);
  bool detectCrossing(uint8_t filtered_rssi);
  
  // Lap processing
  void recordLap(uint32_t timestamp, uint8_t peak_rssi);
  
  // FreeRTOS task function
  static void timingTask(void* parameter);
  
public:
  TimingCore();
  
  // Core functions
  void begin();
  void process();
  void reset();
  
  // Configuration
  void setFrequency(uint16_t freq_mhz);
  void setThreshold(uint8_t threshold);
  void setActivated(bool active);
  void setDebugMode(bool debug_enabled);
  
  // State access (thread-safe)
  TimingState getState() const;
  uint8_t getCurrentRSSI() const;
  uint8_t getPeakRSSI() const;
  uint16_t getLapCount() const;
  bool isActivated() const;
  bool isCrossing() const;
  
  // Lap data access
  bool hasNewLap();
  LapData getNextLap();
  LapData getLastLap();
  uint8_t getAvailableLaps();
  
  // Callbacks for mode-specific handling
  typedef void (*LapCallback)(const LapData& lap);
  typedef void (*CrossingCallback)(bool crossing_state, uint8_t rssi);
  
  void setLapCallback(LapCallback callback) { lap_callback = callback; }
  void setCrossingCallback(CrossingCallback callback) { crossing_callback = callback; }
  
  // Hardware diagnostics
  void testSPIPins();
  void testChannelPinMode();
  void testChannelPinModeLow();
  void testChannelPinModeHigh();
  
private:
  LapCallback lap_callback = nullptr;
  CrossingCallback crossing_callback = nullptr;
};

#endif // TIMING_CORE_H
