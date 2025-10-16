#include "timing_core.h"
#include <SPI.h>

// RX5808 register definitions
#define RX5808_WRITE_REGISTER   0x00
#define RX5808_SYNTH_A_REGISTER 0x01
#define RX5808_SYNTH_B_REGISTER 0x02

// RX5808 timing constants
#define RX5808_MIN_TUNETIME 35    // ms - wait after freq change before reading RSSI
#define RX5808_MIN_BUSTIME  30    // ms - minimum time between freq changes

TimingCore::TimingCore() {
  // Initialize state
  memset(&state, 0, sizeof(state));
  state.threshold = CROSSING_THRESHOLD;
  state.frequency_mhz = DEFAULT_FREQ;
  
  // Initialize buffers
  memset(lap_buffer, 0, sizeof(lap_buffer));
  memset(rssi_samples, 0, sizeof(rssi_samples));
  
  lap_write_index = 0;
  lap_read_index = 0;
  sample_index = 0;
  samples_filled = false;
  debug_enabled = false; // Default to no debug output
  recent_freq_change = false;
  freq_change_time = 0;
  
  // Initialize FreeRTOS objects
  timing_task_handle = nullptr;
  timing_mutex = xSemaphoreCreateMutex();
}

void TimingCore::begin() {
  if (debug_enabled) {
    Serial.println("TimingCore: Initializing...");
  }
  
  // Setup pins
  pinMode(RSSI_INPUT_PIN, INPUT);
  
  // CRITICAL: Configure ADC attenuation for full 0-3.3V range
  // RX5808 RSSI output is 0-3.3V, so we need 11dB attenuation
  // Without this, ESP32-C3 ADC defaults to lower voltage range and saturates
  analogSetAttenuation(ADC_11db);  // Set global attenuation to 11dB (0-3.3V)
  
  if (debug_enabled) {
    Serial.println("ADC configured for 0-3.3V range (11dB attenuation)");
  }
  
  // Test ADC reading immediately
  uint16_t test_adc = analogRead(RSSI_INPUT_PIN);
  if (debug_enabled) {
    Serial.printf("ADC test reading on pin %d: %d (raw 12-bit)\n", RSSI_INPUT_PIN, test_adc);
    uint16_t clamped = (test_adc > 2047) ? 2047 : test_adc;
    uint8_t final_rssi = clamped >> 3;
    Serial.printf("Clamped: %d, Final RSSI: %d (0-255 range)\n", clamped, final_rssi);
  }
  
  // Initialize RX5808 module
  setupRX5808();
  
  // Set default frequency
  setRX5808Frequency(state.frequency_mhz);
  
  // Initialize RSSI filtering with some test readings
  for (int i = 0; i < RSSI_SAMPLES; i++) {
    rssi_samples[i] = readRawRSSI(); // Use the new scaling function
    if (debug_enabled) {
      Serial.printf("Initial RSSI sample %d: %d\n", i, rssi_samples[i]);
    }
  }
  
  // Note: RSSI calibration will be done after debug mode is set
  
  // Create timing task for ESP32-C3 single core (high priority)
  xTaskCreate(timingTask, "TimingTask", 4096, this, TIMING_PRIORITY, &timing_task_handle);
  
  // Mark as activated
  state.activated = true;
  
  if (debug_enabled) {
    Serial.println("TimingCore: Ready");
  }
}

void TimingCore::process() {
  // For ESP32-C3, timing is handled by the dedicated task
  // This method is kept for compatibility but does minimal work
  if (!state.activated) {
    return;
  }
  
  // Just yield to allow other tasks to run
  vTaskDelay(pdMS_TO_TICKS(1));
}

// FreeRTOS task for timing processing (ESP32-C3 single core)
void TimingCore::timingTask(void* parameter) {
  TimingCore* core = static_cast<TimingCore*>(parameter);
  uint32_t debug_counter = 0;
  
  // Performance monitoring variables
  uint32_t loop_count = 0;
  uint32_t last_perf_time = 0;
  uint32_t min_loop_time = UINT32_MAX;
  uint32_t max_loop_time = 0;
  uint32_t total_loop_time = 0;
  
  while (true) {
    if (!core->state.activated) {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    
    uint32_t loop_start = micros();
    static uint32_t last_process_time = 0;
    uint32_t current_time = millis();
    
    // Limit processing to configured interval
    if (current_time - last_process_time < TIMING_INTERVAL_MS) {
      vTaskDelay(pdMS_TO_TICKS(1));
      continue;
    }
    
    // Take mutex for thread safety
    if (xSemaphoreTake(core->timing_mutex, portMAX_DELAY)) {
      // Read and filter RSSI
      uint8_t raw_rssi = core->readRawRSSI();
      
      // Debug output every 1000 iterations (about once per second) - only in debug mode
      debug_counter++;
      if (debug_counter % 1000 == 0 && core->debug_enabled) {
        uint16_t raw_adc = analogRead(RSSI_INPUT_PIN);
        uint16_t clamped = (raw_adc > 2047) ? 2047 : raw_adc;
        Serial.printf("[TimingTask] ADC: %d, Clamped: %d, RSSI: %d, Threshold: %d, Crossing: %s, FreqStable: %s\n", 
                      raw_adc, clamped, raw_rssi, core->state.threshold, 
                      (raw_rssi >= core->state.threshold) ? "YES" : "NO",
                      core->recent_freq_change ? "NO" : "YES");
      }
      
      uint8_t filtered_rssi = core->filterRSSI(raw_rssi);
      core->state.current_rssi = filtered_rssi;
      
      // Update peak tracking
      if (filtered_rssi > core->state.peak_rssi) {
        core->state.peak_rssi = filtered_rssi;
      }
      
      // Detect crossing events
      bool crossing_detected = core->detectCrossing(filtered_rssi);
      
      // Handle crossing state changes
      if (crossing_detected != core->state.crossing_active) {
        core->state.crossing_active = crossing_detected;
        
        if (crossing_detected) {
          // Starting a crossing
          core->state.crossing_start = current_time;
          if (core->debug_enabled) {
            Serial.printf("Crossing started - RSSI: %d\n", filtered_rssi);
          }
        } else {
          // Ending a crossing - record lap
          uint32_t crossing_duration = current_time - core->state.crossing_start;
          if (crossing_duration > 100) { // Minimum 100ms crossing to avoid noise
            core->recordLap(current_time, core->state.peak_rssi);
          }
          if (core->debug_enabled) {
            Serial.printf("Crossing ended - Duration: %dms\n", crossing_duration);
          }
        }
        
        // Notify callback if registered  
        if (core->crossing_callback) {
          core->crossing_callback(core->state.crossing_active, filtered_rssi);
        }
      }
      
      last_process_time = current_time;
      xSemaphoreGive(core->timing_mutex);
    }
    
    // Performance monitoring
    uint32_t loop_end = micros();
    uint32_t loop_time = loop_end - loop_start;
    
    loop_count++;
    if (loop_time < min_loop_time) min_loop_time = loop_time;
    if (loop_time > max_loop_time) max_loop_time = loop_time;
    total_loop_time += loop_time;
    
    // Report performance every 5 seconds - only in debug mode
    uint32_t now = millis();
    if (now - last_perf_time >= 5000) {
      if (core->debug_enabled) {
        uint32_t avg_loop_time = total_loop_time / loop_count;
        uint32_t loops_per_second = (loop_count * 1000) / (now - last_perf_time);
        
        Serial.printf("[TimingPerf] Loops/sec: %d, Avg: %dus, Min: %dus, Max: %dus\n", 
                      loops_per_second, avg_loop_time, min_loop_time, max_loop_time);
      }
      
      // Reset counters
      loop_count = 0;
      last_perf_time = now;
      min_loop_time = UINT32_MAX;
      max_loop_time = 0;
      total_loop_time = 0;
    }
    
    // Small delay to prevent task from consuming all CPU
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

uint8_t TimingCore::readRawRSSI() {
  // Check if frequency was recently changed - RSSI is unstable during tuning
  // This is critical for accurate readings after frequency changes
  if (recent_freq_change) {
    uint32_t elapsed = millis() - freq_change_time;
    if (elapsed < RX5808_MIN_TUNETIME) {
      // RSSI is still unstable, return 0
      return 0;
    } else {
      // Tuning complete, clear flag
      recent_freq_change = false;
    }
  }
  
  // Read 12-bit ADC value (0-4095 on ESP32)
  uint16_t adc_value = analogRead(RSSI_INPUT_PIN);
  
  // RX5808 RSSI typically outputs 0-2V (not full 0-3.3V range)
  // Clamp to 2047 (half of 12-bit range = ~2V @ 3.3V reference)
  // This prevents noise in the unused upper range from affecting readings
  if (adc_value > 2047) {
    adc_value = 2047;
  }
  
  // Rescale from 0-2047 to 0-255 (divide by 8)
  // This gives us the standard 0-255 RSSI range used by lap timing systems
  return adc_value >> 3;
}

uint8_t TimingCore::filterRSSI(uint8_t raw_rssi) {
  // Simple moving average filter
  rssi_samples[sample_index] = raw_rssi;
  sample_index = (sample_index + 1) % RSSI_SAMPLES;
  
  if (!samples_filled && sample_index == 0) {
    samples_filled = true;
  }
  
  // Calculate average
  uint32_t sum = 0;
  uint8_t count = samples_filled ? RSSI_SAMPLES : sample_index;
  
  for (uint8_t i = 0; i < count; i++) {
    sum += rssi_samples[i];
  }
  
  return (count > 0) ? (sum / count) : raw_rssi;
}

bool TimingCore::detectCrossing(uint8_t filtered_rssi) {
  // Simple threshold-based crossing detection
  return filtered_rssi >= state.threshold;
}

void TimingCore::recordLap(uint32_t timestamp, uint8_t peak_rssi) {
  LapData& lap = lap_buffer[lap_write_index];
  
  lap.timestamp_ms = timestamp;
  lap.lap_time_ms = (state.last_lap_time > 0) ? 
                   (timestamp - state.last_lap_time) : 0;
  lap.rssi_peak = peak_rssi;
  lap.pilot_id = 0; // Single pilot for now
  lap.valid = true;
  
  // Update state
  state.last_lap_time = timestamp;
  state.lap_count++;
  
  // Advance write index
  lap_write_index = (lap_write_index + 1) % MAX_LAPS_STORED;
  
  // Reset peak tracking
  state.peak_rssi = 0;
  
        // Debug output disabled to avoid interfering with serial protocol
        // DEBUG_PRINT("Lap recorded: ");
        // DEBUG_PRINT(state.lap_count);
        // DEBUG_PRINT(", Time: ");
        // DEBUG_PRINT(lap.lap_time_ms);
        // DEBUG_PRINT("ms, Peak: ");
        // DEBUG_PRINTLN(peak_rssi);
  
  // Notify callback if registered
  if (lap_callback) {
    lap_callback(lap);
  }
}

void TimingCore::setupRX5808() {
  if (debug_enabled) {
    Serial.println("Setting up RX5808...");
  }
  
  // Initialize SPI pins
  pinMode(RX5808_DATA_PIN, OUTPUT);
  pinMode(RX5808_CLK_PIN, OUTPUT);
  pinMode(RX5808_SEL_PIN, OUTPUT);
  
  if (debug_enabled) {
    Serial.printf("RX5808 pins - DATA: %d, CLK: %d, SEL: %d\n", 
                  RX5808_DATA_PIN, RX5808_CLK_PIN, RX5808_SEL_PIN);
  }
  
  // Set initial states
  digitalWrite(RX5808_SEL_PIN, HIGH);
  digitalWrite(RX5808_CLK_PIN, LOW);
  digitalWrite(RX5808_DATA_PIN, LOW);
  
  delay(100); // Allow module to stabilize
  
  // CRITICAL: Reset RX5808 module to wake it and put it in a known state
  // This ensures reliable operation after power-on
  resetRX5808Module();
  
  // CRITICAL: Configure power settings to optimize performance
  // This disables unused features and reduces power consumption
  configureRX5808Power();
  
  if (debug_enabled) {
    Serial.println("RX5808 setup complete (reset and configured)");
  }
}

void TimingCore::setRX5808Frequency(uint16_t freq_mhz) {
  if (freq_mhz < MIN_FREQ || freq_mhz > MAX_FREQ) {
    if (debug_enabled) {
      Serial.printf("Invalid frequency: %d MHz (valid range: %d-%d)\n", freq_mhz, MIN_FREQ, MAX_FREQ);
    }
    return;
  }
  
  // Calculate register value using standard RX5808 formula from datasheet
  // Formula: tf = (freq - 479) / 2, then N = tf / 32, A = tf % 32, reg = (N << 7) + A
  uint16_t tf = (freq_mhz - 479) / 2;
  uint16_t N = tf / 32;
  uint16_t A = tf % 32;
  uint16_t vtxHex = (N << 7) + A;
  
  if (debug_enabled) {
    Serial.printf("Setting frequency to %d MHz (tf=%d, N=%d, A=%d, reg=0x%04X)\n", 
                  freq_mhz, tf, N, A, vtxHex);
  }
  
  // Send frequency to RX5808 using standard 25-bit protocol:
  // 4 bits: Register address (0x1)
  // 1 bit: Write flag (1)
  // 16 bits: Register value (LSB first)
  // 4 bits: Padding (0)
  
  digitalWrite(RX5808_SEL_PIN, HIGH);
  digitalWrite(RX5808_SEL_PIN, LOW);
  
  // Register 0x1 (frequency register)
  sendRX5808Bit(1);  // bit 0
  sendRX5808Bit(0);  // bit 1
  sendRX5808Bit(0);  // bit 2
  sendRX5808Bit(0);  // bit 3
  
  // Write flag
  sendRX5808Bit(1);
  
  // Data bits D0-D15 (LSB first)
  for (uint8_t i = 0; i < 16; i++) {
    sendRX5808Bit((vtxHex >> i) & 0x1);
  }
  
  // Padding bits D16-D19
  sendRX5808Bit(0);
  sendRX5808Bit(0);
  sendRX5808Bit(0);
  sendRX5808Bit(0);
  
  digitalWrite(RX5808_SEL_PIN, HIGH);
  delay(2);
  
  digitalWrite(RX5808_CLK_PIN, LOW);
  digitalWrite(RX5808_DATA_PIN, LOW);
  
  state.frequency_mhz = freq_mhz;
  
  // Mark frequency change time to prevent unstable RSSI reads
  recent_freq_change = true;
  freq_change_time = millis();
  
  if (debug_enabled) {
    Serial.printf("Frequency set to %d MHz (RSSI unstable for %dms)\n", freq_mhz, RX5808_MIN_TUNETIME);
  }
}

void TimingCore::sendRX5808Bit(uint8_t bit_value) {
  // Send a single bit to RX5808 using standard SPI-like protocol
  // 300µs delays ensure reliable communication with the module
  digitalWrite(RX5808_DATA_PIN, bit_value ? HIGH : LOW);
  delayMicroseconds(300);
  
  digitalWrite(RX5808_CLK_PIN, HIGH);
  delayMicroseconds(300);
  
  digitalWrite(RX5808_CLK_PIN, LOW);
  delayMicroseconds(300);
}

void TimingCore::resetRX5808Module() {
  // Reset RX5808 by writing zeros to register 0xF
  // This wakes the module from power-down and puts it in a known state
  if (debug_enabled) {
    Serial.println("Resetting RX5808 module (register 0xF)...");
  }
  
  digitalWrite(RX5808_SEL_PIN, HIGH);
  digitalWrite(RX5808_SEL_PIN, LOW);
  
  // Register 0xF (reset register) = 1111
  sendRX5808Bit(1);
  sendRX5808Bit(1);
  sendRX5808Bit(1);
  sendRX5808Bit(1);
  
  // Write flag
  sendRX5808Bit(1);
  
  // 20 bits of zeros
  for (uint8_t i = 0; i < 20; i++) {
    sendRX5808Bit(0);
  }
  
  digitalWrite(RX5808_SEL_PIN, HIGH);
  delay(10);
  
  if (debug_enabled) {
    Serial.println("RX5808 reset complete");
  }
}

void TimingCore::configureRX5808Power() {
  // Configure RX5808 power register 0xA for optimal operation
  // This disables unused features to save power and optimize performance
  // Power configuration: 0b11010000110111110011
  if (debug_enabled) {
    Serial.println("Configuring RX5808 power (register 0xA)...");
  }
  
  digitalWrite(RX5808_SEL_PIN, HIGH);
  digitalWrite(RX5808_SEL_PIN, LOW);
  
  // Register 0xA (power register) = 1010
  sendRX5808Bit(0);
  sendRX5808Bit(1);
  sendRX5808Bit(0);
  sendRX5808Bit(1);
  
  // Write flag
  sendRX5808Bit(1);
  
  // 20 bits of power configuration data: 0b11010000110111110011
  uint32_t power_config = 0b11010000110111110011;
  for (uint8_t i = 0; i < 20; i++) {
    sendRX5808Bit((power_config >> i) & 0x1);
  }
  
  digitalWrite(RX5808_SEL_PIN, HIGH);
  delay(10);
  
  digitalWrite(RX5808_DATA_PIN, LOW);
  
  if (debug_enabled) {
    Serial.println("RX5808 power configuration complete");
  }
}

// Public interface methods (thread-safe for ESP32-C3)
void TimingCore::setFrequency(uint16_t freq_mhz) {
  if (xSemaphoreTake(timing_mutex, portMAX_DELAY)) {
    setRX5808Frequency(freq_mhz);
    xSemaphoreGive(timing_mutex);
  }
}

void TimingCore::setThreshold(uint8_t threshold) {
  if (xSemaphoreTake(timing_mutex, portMAX_DELAY)) {
    state.threshold = threshold;
    // Threshold set
    xSemaphoreGive(timing_mutex);
  }
}

void TimingCore::setActivated(bool active) {
  if (xSemaphoreTake(timing_mutex, portMAX_DELAY)) {
    state.activated = active;
    // Timing activated/deactivated
    xSemaphoreGive(timing_mutex);
  }
}

void TimingCore::reset() {
  if (xSemaphoreTake(timing_mutex, portMAX_DELAY)) {
    state.lap_count = 0;
    state.last_lap_time = 0;
    state.peak_rssi = 0;
    state.crossing_active = false;
    
    // Clear lap buffer
    memset(lap_buffer, 0, sizeof(lap_buffer));
    lap_write_index = 0;
    lap_read_index = 0;
    
    // Timing reset
    xSemaphoreGive(timing_mutex);
  }
}

bool TimingCore::hasNewLap() {
  return lap_read_index != lap_write_index;
}

LapData TimingCore::getNextLap() {
  if (!hasNewLap()) {
    LapData empty = {0};
    return empty;
  }
  
  LapData lap = lap_buffer[lap_read_index];
  lap_read_index = (lap_read_index + 1) % MAX_LAPS_STORED;
  return lap;
}

LapData TimingCore::getLastLap() {
  if (state.lap_count == 0) {
    LapData empty = {0};
    return empty;
  }
  
  uint8_t last_index = (lap_write_index - 1 + MAX_LAPS_STORED) % MAX_LAPS_STORED;
  return lap_buffer[last_index];
}

uint8_t TimingCore::getAvailableLaps() {
  return (lap_write_index - lap_read_index + MAX_LAPS_STORED) % MAX_LAPS_STORED;
}

// Thread-safe state access methods
TimingState TimingCore::getState() const {
  TimingState current_state;
  if (xSemaphoreTake(timing_mutex, pdMS_TO_TICKS(10))) {
    current_state = state;
    xSemaphoreGive(timing_mutex);
  } else {
    // If we can't get the mutex quickly, return a zeroed state
    memset(&current_state, 0, sizeof(current_state));
  }
  return current_state;
}

uint8_t TimingCore::getCurrentRSSI() const {
  uint8_t rssi = 0;
  if (xSemaphoreTake(timing_mutex, pdMS_TO_TICKS(10))) {
    rssi = state.current_rssi;
    xSemaphoreGive(timing_mutex);
  }
  return rssi;
}

uint8_t TimingCore::getPeakRSSI() const {
  uint8_t peak = 0;
  if (xSemaphoreTake(timing_mutex, pdMS_TO_TICKS(10))) {
    peak = state.peak_rssi;
    xSemaphoreGive(timing_mutex);
  }
  return peak;
}

uint16_t TimingCore::getLapCount() const {
  uint16_t count = 0;
  if (xSemaphoreTake(timing_mutex, pdMS_TO_TICKS(10))) {
    count = state.lap_count;
    xSemaphoreGive(timing_mutex);
  }
  return count;
}

bool TimingCore::isActivated() const {
  bool activated = false;
  if (xSemaphoreTake(timing_mutex, pdMS_TO_TICKS(10))) {
    activated = state.activated;
    xSemaphoreGive(timing_mutex);
  }
  return activated;
}

bool TimingCore::isCrossing() const {
  bool crossing = false;
  if (xSemaphoreTake(timing_mutex, pdMS_TO_TICKS(10))) {
    crossing = state.crossing_active;
    xSemaphoreGive(timing_mutex);
  }
  return crossing;
}

void TimingCore::setDebugMode(bool debug_enabled) {
  this->debug_enabled = debug_enabled;
}

