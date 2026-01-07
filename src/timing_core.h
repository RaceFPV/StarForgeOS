#ifndef TIMING_CORE_H
#define TIMING_CORE_H

#include <Arduino.h>
#include <cstdint>
#include "config/config.h"
#include "esp_adc/adc_continuous.h"
#include "filters/kalman.h"

// Structure to hold lap data
struct LapData {
  uint32_t timestamp_ms;    // Lap timestamp in milliseconds
  uint16_t lap_time_ms;     // Time since last lap (0 for first lap)
  uint8_t rssi_peak;        // Peak RSSI during this lap
  uint8_t pilot_id;         // Pilot ID (0-based)
  bool valid;               // Whether this lap data is valid
};

// Structure to hold RSSI extremum data (for marshal mode history)
struct Extremum {
  uint8_t rssi;             // RSSI value at extremum
  uint32_t first_time;      // Timestamp when extremum started (milliseconds)
  uint16_t duration;        // Duration extremum was held (milliseconds)
  bool valid;               // Whether this extremum is valid
};

// Structure to hold RSSI sample with timestamp (for race history export)
struct RSSISample {
  uint32_t timestamp_ms;    // Timestamp in milliseconds since race start
  uint8_t rssi;             // Filtered RSSI value
};

#define EXTREMUM_BUFFER_SIZE 256  // Circular buffer for extremums (must be power of 2)

// Structure to hold current timing state
struct TimingState {
  uint8_t current_rssi;     // Current filtered RSSI value
  uint8_t peak_rssi;        // Peak RSSI since last reset
  uint8_t nadir_rssi;       // Lowest RSSI since last reset
  uint8_t enter_rssi;       // Enter threshold (start capturing peak)
  uint8_t exit_rssi;        // Exit threshold (detect lap completion)
  bool crossing_active;     // Whether we're currently in a crossing
  uint32_t crossing_start;  // When current crossing started
  uint32_t last_lap_time;   // Timestamp of last lap
  uint16_t lap_count;       // Number of laps completed
  uint16_t frequency_mhz;   // Current RX frequency
  bool activated;           // Whether timing is active
  
  // Extremum tracking state
  uint8_t last_rssi;        // Previous RSSI for trend detection
  int8_t rssi_change;       // >0 rising, <0 falling, 0 stable
  uint8_t pass_rssi_nadir;  // Lowest RSSI since end of last pass
};

class TimingCore {
private:
  TimingState state;
  LapData lap_buffer[MAX_LAPS_STORED];
  uint8_t lap_write_index;
  uint8_t lap_read_index;
  
  // DMA ADC configuration
  adc_continuous_handle_t adc_handle;
  bool use_dma;
  uint8_t* dma_result_buffer;
  
  // RSSI filtering - Kalman filter
  KalmanFilter rssi_filter;
  
  // Peak tracking
  uint8_t rssi_peak;           // Current peak RSSI value
  uint32_t rssi_peak_time_ms;  // Timestamp when peak occurred
  uint32_t lap_start_time_ms;  // Timestamp when current lap started
  uint32_t race_start_time_ms; // Timestamp when race started
  uint32_t min_lap_ms;         // Minimum lap time configuration
  
  // Extremum tracking (circular buffers for marshal mode)
  Extremum peak_buffer[EXTREMUM_BUFFER_SIZE];
  uint8_t peak_write_index;
  uint8_t peak_read_index;
  
  Extremum nadir_buffer[EXTREMUM_BUFFER_SIZE];
  uint8_t nadir_write_index;
  uint8_t nadir_read_index;
  
  // Current extremum being tracked
  Extremum current_peak;
  Extremum current_nadir;

  // RSSI history buffering (for race data export)
#if RSSI_HISTORY_ENABLED
  RSSISample* rssi_history_buffer;  // Dynamically allocated circular buffer
  uint32_t rssi_history_write_index;
  uint32_t rssi_history_count;
  uint32_t last_rssi_sample_time;
  bool rssi_history_allocation_attempted;  // Track if we've tried to allocate (prevent spam)
#endif

  // FreeRTOS task handle for ESP32-C3 single core
  TaskHandle_t timing_task_handle;
  SemaphoreHandle_t timing_mutex;

  // Debug mode flag
  bool debug_enabled;

  // RX5808 frequency stability tracking
  bool recent_freq_change;
  uint32_t freq_change_time;
  uint8_t current_band;
  uint8_t current_channel;
  
  // RX5808 control
  void setupRX5808();
  void resetRX5808Module();
  void configureRX5808Power();
  void setRX5808Frequency(uint16_t freq_mhz);
  void sendRX5808Bit(uint8_t bit_value);
  
  // DMA ADC setup
  void setupADC_DMA();
  void stopADC_DMA();
  
  // RSSI processing
  uint8_t readRawRSSI();
  uint8_t readRawRSSI_DMA();
  uint8_t filterRSSI(uint8_t raw_rssi);
  bool detectCrossing(uint8_t filtered_rssi);
  
  // Lap processing
  void recordLap(uint32_t timestamp, uint8_t peak_rssi);
  
  // Extremum tracking
  void processExtremums(uint32_t timestamp, uint8_t filtered_rssi);
  void finalizePeak(uint32_t timestamp);
  void finalizeNadir(uint32_t timestamp);
  void bufferPeak(const Extremum& peak);
  void bufferNadir(const Extremum& nadir);
  
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
  void setEnterRssi(uint8_t enter_rssi);
  void setExitRssi(uint8_t exit_rssi);
  void setThreshold(uint8_t threshold);  // DEPRECATED: Use setEnterRssi/setExitRssi instead
  void setMinLapMs(uint32_t min_lap_ms);
  void setActivated(bool active);
  void setDebugMode(bool debug_enabled);
  void setRX5808Settings(uint8_t band, uint8_t channel);
  
  // Temporarily pause timing core (for file serving, etc.)
  // Returns previous activation state so it can be restored
  bool pauseTemporarily(uint32_t pauseMs = 500);
  void resumeFromPause(bool wasActive);
  
#if RSSI_HISTORY_ENABLED
  // Allocate RSSI history buffer early (call after WiFi init, before web server)
  // Returns true if allocation succeeded
  bool allocateRSSIHistory();
#endif
  
  // Configuration getters
  uint8_t getEnterRssi() const;
  uint8_t getExitRssi() const;
  uint8_t getThreshold() const;  // DEPRECATED: Returns enter_rssi for compatibility
  uint32_t getMinLapMs() const;
  uint16_t getCurrentFrequency() const;
  void getRX5808Settings(uint8_t& band, uint8_t& channel) const;
  
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
  
  // Extremum data access (for marshal mode)
  bool hasPendingPeak();
  bool hasPendingNadir();
  Extremum getNextPeak();
  Extremum getNextNadir();
  Extremum peekNextPeak() const;  // Peek without removing
  Extremum peekNextNadir() const;  // Peek without removing
  uint8_t getNadirRSSI() const;
  uint8_t getPassNadirRSSI() const;

  // RSSI history access (for race data export)
#if RSSI_HISTORY_ENABLED
  uint32_t getRSSIHistoryCount() const;
  bool getRSSIHistorySample(uint32_t index, RSSISample& sample) const;
  void clearRSSIHistory();
#endif

  // Callbacks for mode-specific handling
  typedef void (*LapCallback)(const LapData& lap);
  typedef void (*CrossingCallback)(bool crossing_state, uint8_t rssi);
  
  void setLapCallback(LapCallback callback) { lap_callback = callback; }
  void setCrossingCallback(CrossingCallback callback) { crossing_callback = callback; }
  
private:
  LapCallback lap_callback = nullptr;
  CrossingCallback crossing_callback = nullptr;
};

#endif // TIMING_CORE_H
