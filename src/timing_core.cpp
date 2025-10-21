#include "timing_core.h"
#include <SPI.h>

// RX5808 register definitions
#define RX5808_WRITE_REGISTER   0x00
#define RX5808_SYNTH_A_REGISTER 0x01
#define RX5808_SYNTH_B_REGISTER 0x02

// RX5808 timing constants
#define RX5808_MIN_TUNETIME 35    // ms - wait after freq change before reading RSSI
#define RX5808_MIN_BUSTIME  30    // ms - minimum time between freq changes

// Static variable to track last RX5808 bus access time (shared across all instances)
static uint32_t lastRX5808BusTimeMs = 0;

TimingCore::TimingCore() {
  // Initialize state
  memset(&state, 0, sizeof(state));
  state.threshold = CROSSING_THRESHOLD;
  state.frequency_mhz = DEFAULT_FREQ;
  state.nadir_rssi = 255;  // Initialize to max value
  state.pass_rssi_nadir = 255;
  state.last_rssi = 0;
  state.rssi_change = 0;
  
  // Initialize buffers
  memset(lap_buffer, 0, sizeof(lap_buffer));
  memset(rssi_samples, 0, sizeof(rssi_samples));
  memset(peak_buffer, 0, sizeof(peak_buffer));
  memset(nadir_buffer, 0, sizeof(nadir_buffer));
  
  lap_write_index = 0;
  lap_read_index = 0;
  sample_index = 0;
  samples_filled = false;
  debug_enabled = false; // Default to no debug output
  recent_freq_change = false;
  freq_change_time = 0;
  
  // Initialize extremum tracking
  peak_write_index = 0;
  peak_read_index = 0;
  nadir_write_index = 0;
  nadir_read_index = 0;
  
  memset(&current_peak, 0, sizeof(current_peak));
  memset(&current_nadir, 0, sizeof(current_nadir));
  current_nadir.rssi = 255;
  
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
  
  // Create timing task for ESP32-C3 single core
  // NOTE: Task starts INACTIVE - must call setActivated(true) after mode initialization
  // This prevents the task from consuming CPU before serial/WiFi setup completes on single-core ESP32-C3
  xTaskCreate(timingTask, "TimingTask", 4096, this, TIMING_PRIORITY, &timing_task_handle);
  
  // Do NOT activate here - will be activated after mode-specific initialization
  // state.activated = true;  // REMOVED - see main.cpp setup() for activation timing
  
  if (debug_enabled) {
    Serial.println("TimingCore: Ready (inactive until mode init)");
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
      
      // Update nadir tracking
      if (filtered_rssi < core->state.nadir_rssi) {
        core->state.nadir_rssi = filtered_rssi;
      }
      if (filtered_rssi < core->state.pass_rssi_nadir) {
        core->state.pass_rssi_nadir = filtered_rssi;
      }
      
      // Process extremums for marshal mode history
      core->processExtremums(current_time, filtered_rssi);
      
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
            // Reset pass nadir after crossing ends
            core->state.pass_rssi_nadir = 255;
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
    
    // Yield to other tasks (especially main loop for serial processing on single-core ESP32-C3)
    taskYIELD();
    
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
  // Wait for tuning to complete before reading (same as RotorHazard)
  if (recent_freq_change) {
    uint32_t timeVal = millis() - freq_change_time;
    if (timeVal < RX5808_MIN_TUNETIME) {
      delay(RX5808_MIN_TUNETIME - timeVal);  // wait until after-tune-delay time is fulfilled
    }
    recent_freq_change = false;  // don't need to check again until next freq change
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

// Extremum tracking implementation (for marshal mode)
void TimingCore::processExtremums(uint32_t timestamp, uint8_t filtered_rssi) {
  // Detect RSSI change direction
  int8_t new_change = 0;
  if (filtered_rssi > state.last_rssi) {
    new_change = 1;  // Rising
  } else if (filtered_rssi < state.last_rssi) {
    new_change = -1; // Falling
  }
  
  // Detect when trend changes (peak or nadir found)
  if (state.rssi_change != new_change && new_change != 0) {
    if (state.rssi_change > 0) {
      // Was rising, now falling = peak found
      finalizePeak(timestamp);
    } else if (state.rssi_change < 0) {
      // Was falling, now rising = nadir found
      finalizeNadir(timestamp);
    }
  }
  
  // Update current extremums
  if (new_change > 0 || state.rssi_change == 0) {
    // Rising or first sample - track peak
    if (filtered_rssi > current_peak.rssi || !current_peak.valid) {
      current_peak.rssi = filtered_rssi;
      if (!current_peak.valid) {
        current_peak.first_time = timestamp;
        current_peak.valid = true;
      }
    }
  }
  
  if (new_change < 0 || state.rssi_change == 0) {
    // Falling or first sample - track nadir
    if (filtered_rssi < current_nadir.rssi || !current_nadir.valid) {
      current_nadir.rssi = filtered_rssi;
      if (!current_nadir.valid) {
        current_nadir.first_time = timestamp;
        current_nadir.valid = true;
      }
    }
  }
  
  // Update state
  state.last_rssi = filtered_rssi;
  if (new_change != 0) {
    state.rssi_change = new_change;
  }
}

void TimingCore::finalizePeak(uint32_t timestamp) {
  if (current_peak.valid && current_peak.rssi > 0) {
    current_peak.duration = timestamp - current_peak.first_time;
    bufferPeak(current_peak);
  }
  
  // Reset for next peak
  memset(&current_peak, 0, sizeof(current_peak));
}

void TimingCore::finalizeNadir(uint32_t timestamp) {
  if (current_nadir.valid && current_nadir.rssi < 255) {
    current_nadir.duration = timestamp - current_nadir.first_time;
    bufferNadir(current_nadir);
  }
  
  // Reset for next nadir
  memset(&current_nadir, 0, sizeof(current_nadir));
  current_nadir.rssi = 255;
}

void TimingCore::bufferPeak(const Extremum& peak) {
  peak_buffer[peak_write_index] = peak;
  peak_write_index = (peak_write_index + 1) & (EXTREMUM_BUFFER_SIZE - 1);
  
  // Check for overflow (if write catches read, drop oldest)
  if (peak_write_index == peak_read_index) {
    peak_read_index = (peak_read_index + 1) & (EXTREMUM_BUFFER_SIZE - 1);
  }
}

void TimingCore::bufferNadir(const Extremum& nadir) {
  nadir_buffer[nadir_write_index] = nadir;
  nadir_write_index = (nadir_write_index + 1) & (EXTREMUM_BUFFER_SIZE - 1);
  
  // Check for overflow (if write catches read, drop oldest)
  if (nadir_write_index == nadir_read_index) {
    nadir_read_index = (nadir_read_index + 1) & (EXTREMUM_BUFFER_SIZE - 1);
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
  
  // CRITICAL: Configure power settings (called after reset, same as RotorHazard)
  // This disables unused features and reduces power consumption
  configureRX5808Power();
  
  if (debug_enabled) {
    Serial.println("RX5808 setup complete (reset and configured)");
  }
}

void TimingCore::setRX5808Frequency(uint16_t freq_mhz) {
  // Check if enough time has elapsed since last RX5808 bus access
  // This prevents bus conflicts and ensures reliable communication
  uint32_t timeVal = millis() - lastRX5808BusTimeMs;
  if (timeVal < RX5808_MIN_BUSTIME) {
    delay(RX5808_MIN_BUSTIME - timeVal);  // wait until after-bus-delay time is fulfilled
  }
  
  if (freq_mhz < MIN_FREQ || freq_mhz > MAX_FREQ) {
    if (debug_enabled) {
      Serial.printf("Invalid frequency: %d MHz (valid range: %d-%d)\n", freq_mhz, MIN_FREQ, MAX_FREQ);
    }
    return;
  }
  
  // Calculate register value using RX5808 formula (from RotorHazard node)
  // Formula: tf = (freq - 479) / 2, N = tf / 32, A = tf % 32, reg = (N << 7) + A
  uint16_t tf = (freq_mhz - 479) / 2;
  uint16_t N = tf / 32;
  uint16_t A = tf % 32;
  uint16_t vtxHex = (N << 7) + A;
  
  if (debug_enabled) {
    Serial.printf("\n=== RTC6715 Frequency Change ===\n");
    Serial.printf("Target: %d MHz (tf=%d, N=%d, A=%d, reg=0x%04X)\n", 
                  freq_mhz, tf, N, A, vtxHex);
    Serial.printf("Pins: DATA=%d, CLK=%d, SEL=%d\n", RX5808_DATA_PIN, RX5808_CLK_PIN, RX5808_SEL_PIN);
  }
  
  // Send frequency to RX5808 using standard 25-bit protocol:
  // 4 bits: Register address (0x1)
  // 1 bit: Write flag (1)
  // 16 bits: Register value (LSB first)
  // 4 bits: Padding (0)
  
  if (debug_enabled) {
    Serial.print("Sending bits: ");
  }
  
  digitalWrite(RX5808_SEL_PIN, HIGH);
  digitalWrite(RX5808_SEL_PIN, LOW);
  
  // Register 0x1 (frequency register)
  sendRX5808Bit(1);  // bit 0
  sendRX5808Bit(0);  // bit 1
  sendRX5808Bit(0);  // bit 2
  sendRX5808Bit(0);  // bit 3
  
  if (debug_enabled) {
    Serial.print("0001 ");  // Register 0x1 (LSB first = 1000 binary = 0x1)
  }
  
  // Write flag
  sendRX5808Bit(1);
  
  if (debug_enabled) {
    Serial.print("1 ");  // Write flag
  }
  
  // Data bits D0-D15 (LSB first)
  for (uint8_t i = 0; i < 16; i++) {
    uint8_t bit = (vtxHex >> i) & 0x1;
    sendRX5808Bit(bit);
    if (debug_enabled && i % 4 == 3) {
      Serial.print(" ");  // Space every 4 bits for readability
    }
    if (debug_enabled) {
      Serial.print(bit);
    }
  }
  
  // Padding bits D16-D19
  sendRX5808Bit(0);
  sendRX5808Bit(0);
  sendRX5808Bit(0);
  sendRX5808Bit(0);
  
  if (debug_enabled) {
    Serial.println(" 0000");  // Padding
  }
  
  digitalWrite(RX5808_SEL_PIN, HIGH);
  delay(2);
  
  digitalWrite(RX5808_CLK_PIN, LOW);
  digitalWrite(RX5808_DATA_PIN, LOW);
  
  state.frequency_mhz = freq_mhz;
  
  // Mark frequency change time to prevent unstable RSSI reads
  recent_freq_change = true;
  freq_change_time = millis();
  lastRX5808BusTimeMs = freq_change_time;  // Track last bus access time
  
  if (debug_enabled) {
    Serial.printf("SPI sequence sent successfully\n");
    Serial.printf("Frequency set to %d MHz (RSSI unstable for %dms)\n", freq_mhz, RX5808_MIN_TUNETIME);
    Serial.printf("Waiting for module to tune...\n");
    
    // Wait for tuning, then read RSSI to verify
    delay(RX5808_MIN_TUNETIME + 10);
    uint16_t test_adc = analogRead(RSSI_INPUT_PIN);
    uint8_t test_rssi = (test_adc > 2047 ? 2047 : test_adc) >> 3;
    Serial.printf("RSSI after freq change: %d (ADC: %d)\n", test_rssi, test_adc);
    Serial.printf("If RSSI doesn't change between frequencies, check SPI_EN pin!\n");
    Serial.printf("=================================\n\n");
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
    state.nadir_rssi = 255;
    state.pass_rssi_nadir = 255;
    state.crossing_active = false;
    state.last_rssi = 0;
    state.rssi_change = 0;
    
    // Clear lap buffer
    memset(lap_buffer, 0, sizeof(lap_buffer));
    lap_write_index = 0;
    lap_read_index = 0;
    
    // Clear extremum buffers
    memset(peak_buffer, 0, sizeof(peak_buffer));
    memset(nadir_buffer, 0, sizeof(nadir_buffer));
    peak_write_index = 0;
    peak_read_index = 0;
    nadir_write_index = 0;
    nadir_read_index = 0;
    
    memset(&current_peak, 0, sizeof(current_peak));
    memset(&current_nadir, 0, sizeof(current_nadir));
    current_nadir.rssi = 255;
    
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

// Extremum data access methods (for marshal mode)
bool TimingCore::hasPendingPeak() {
  return peak_read_index != peak_write_index;
}

bool TimingCore::hasPendingNadir() {
  return nadir_read_index != nadir_write_index;
}

Extremum TimingCore::getNextPeak() {
  if (!hasPendingPeak()) {
    Extremum empty = {0};
    return empty;
  }
  
  Extremum peak = peak_buffer[peak_read_index];
  peak_read_index = (peak_read_index + 1) & (EXTREMUM_BUFFER_SIZE - 1);
  return peak;
}

Extremum TimingCore::getNextNadir() {
  if (!hasPendingNadir()) {
    Extremum empty = {255, 0, 0, false};
    return empty;
  }
  
  Extremum nadir = nadir_buffer[nadir_read_index];
  nadir_read_index = (nadir_read_index + 1) & (EXTREMUM_BUFFER_SIZE - 1);
  return nadir;
}

uint8_t TimingCore::getNadirRSSI() const {
  uint8_t nadir = 255;
  if (xSemaphoreTake(timing_mutex, portMAX_DELAY)) {
    nadir = state.nadir_rssi;
    xSemaphoreGive(timing_mutex);
  }
  return nadir;
}

uint8_t TimingCore::getPassNadirRSSI() const {
  uint8_t nadir = 255;
  if (xSemaphoreTake(timing_mutex, portMAX_DELAY)) {
    nadir = state.pass_rssi_nadir;
    xSemaphoreGive(timing_mutex);
  }
  return nadir;
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
  if (xSemaphoreTake(timing_mutex, portMAX_DELAY)) {
    rssi = state.current_rssi;
    xSemaphoreGive(timing_mutex);
  }
  return rssi;
}

uint8_t TimingCore::getPeakRSSI() const {
  uint8_t peak = 0;
  if (xSemaphoreTake(timing_mutex, portMAX_DELAY)) {
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

void TimingCore::testSPIPins() {
  Serial.println("\n=== RTC6715 Hardware Diagnostic ===");
  Serial.println("Testing SPI pin connections...\n");
  
  // Test each pin independently
  Serial.println("1. Testing DATA pin (should toggle HIGH/LOW):");
  Serial.printf("   Pin %d: ", RX5808_DATA_PIN);
  pinMode(RX5808_DATA_PIN, OUTPUT);
  digitalWrite(RX5808_DATA_PIN, HIGH);
  delay(100);
  digitalWrite(RX5808_DATA_PIN, LOW);
  Serial.println("Toggle sent (use oscilloscope/LED to verify)");
  
  Serial.println("\n2. Testing CLK pin (should toggle HIGH/LOW):");
  Serial.printf("   Pin %d: ", RX5808_CLK_PIN);
  pinMode(RX5808_CLK_PIN, OUTPUT);
  digitalWrite(RX5808_CLK_PIN, HIGH);
  delay(100);
  digitalWrite(RX5808_CLK_PIN, LOW);
  Serial.println("Toggle sent (use oscilloscope/LED to verify)");
  
  Serial.println("\n3. Testing SEL pin (should toggle HIGH/LOW):");
  Serial.printf("   Pin %d: ", RX5808_SEL_PIN);
  pinMode(RX5808_SEL_PIN, OUTPUT);
  digitalWrite(RX5808_SEL_PIN, HIGH);
  delay(100);
  digitalWrite(RX5808_SEL_PIN, LOW);
  delay(100);
  digitalWrite(RX5808_SEL_PIN, HIGH);
  Serial.println("Toggle sent (use oscilloscope/LED to verify)");
  
  Serial.println("\n4. Testing RSSI input pin:");
  Serial.printf("   Pin %d (ADC): ", RSSI_INPUT_PIN);
  pinMode(RSSI_INPUT_PIN, INPUT);
  uint16_t adc_val = analogRead(RSSI_INPUT_PIN);
  Serial.printf("Raw ADC = %d (should be 0-4095)\n", adc_val);
  
  Serial.println("\n5. Sending test SPI sequence to RTC6715:");
  Serial.println("   Attempting to set frequency to 5800 MHz...");
  setRX5808Frequency(5800);
  
  Serial.println("\n6. Reading RSSI after frequency set:");
  delay(50);
  adc_val = analogRead(RSSI_INPUT_PIN);
  uint8_t rssi = (adc_val > 2047 ? 2047 : adc_val) >> 3;
  Serial.printf("   RSSI = %d (0-255), ADC = %d\n", rssi, adc_val);
  
  Serial.println("\n=== Hardware Test Complete ===");
  Serial.println("\nNEXT STEPS:");
  Serial.println("1. If RSSI doesn't change between frequencies:");
  Serial.println("   -> RTC6715 is in Channel Pin Mode (SPI_EN grounded)");
  Serial.println("   -> Remove pull-down resistor from SPI_EN pin");
  Serial.println("\n2. If all SPI pins toggle correctly:");
  Serial.println("   -> Wiring is OK, issue is SPI_EN configuration");
  Serial.println("\n3. If SPI pins don't toggle:");
  Serial.println("   -> Check wiring from ESP32 to RTC6715");
  Serial.println("   -> Verify no shorts or broken traces");
  Serial.println("\nSee docs/RTC6715_TROUBLESHOOTING.md for detailed guide");
  Serial.println("=====================================\n");
}

void TimingCore::testChannelPinMode() {
  Serial.println("\n╔════════════════════════════════════════════════════════════════╗");
  Serial.println("║          RTC6715 CHANNEL PIN MODE TEST                        ║");
  Serial.println("╚════════════════════════════════════════════════════════════════╝\n");
  
  Serial.println("This test determines if RTC6715 is in Channel Pin Mode or SPI Mode");
  Serial.println("by forcing pin states to select different channels.\n");
  
  // First, try to set frequency via SPI to something known
  Serial.println("Step 1: Setting frequency to 5658 MHz via SPI...");
  setRX5808Frequency(5658);
  delay(50);
  
  uint16_t adc_spi = analogRead(RSSI_INPUT_PIN);
  uint8_t rssi_spi = (adc_spi > 2047 ? 2047 : adc_spi) >> 3;
  Serial.printf("   RSSI after SPI command: %d (ADC: %d)\n\n", rssi_spi, adc_spi);
  
  pinMode(RX5808_DATA_PIN, OUTPUT);
  pinMode(RX5808_CLK_PIN, OUTPUT);
  pinMode(RX5808_SEL_PIN, OUTPUT);
  
  // Test 1: All pins LOW
  Serial.println("Step 2A: Setting all 3 pins LOW...");
  Serial.println("   In Channel Pin Mode, this selects:");
  Serial.println("   CH1=0, CH2=0, CH3=0 = binary 000 = Channel 0");
  Serial.println("   Expected: 5865 MHz (Boscam A1)\n");
  
  digitalWrite(RX5808_DATA_PIN, LOW);   // CH1 = 0
  digitalWrite(RX5808_CLK_PIN, LOW);    // CH3 = 0
  digitalWrite(RX5808_SEL_PIN, LOW);    // CH2 = 0
  
  Serial.println("   All pins set LOW. Waiting for chip to respond...");
  delay(100);  // Give chip time to respond if in Channel Pin Mode
  
  uint16_t adc_low = analogRead(RSSI_INPUT_PIN);
  uint8_t rssi_low = (adc_low > 2047 ? 2047 : adc_low) >> 3;
  Serial.printf("   RSSI with pins LOW: %d (ADC: %d)\n", rssi_low, adc_low);
  Serial.println("   → If generator on 5865 MHz, RSSI should be high\n");
  
  // Test 2: All pins HIGH
  Serial.println("Step 2B: Setting all 3 pins HIGH...");
  Serial.println("   In Channel Pin Mode, this selects:");
  Serial.println("   CH1=1, CH2=1, CH3=1 = binary 111 = Channel 7");
  Serial.println("   Expected: 5725 MHz (Boscam A8)\n");
  
  digitalWrite(RX5808_DATA_PIN, HIGH);  // CH1 = 1
  digitalWrite(RX5808_CLK_PIN, HIGH);   // CH3 = 1
  digitalWrite(RX5808_SEL_PIN, HIGH);   // CH2 = 1
  
  Serial.println("   All pins set HIGH. Waiting for chip to respond...");
  delay(100);  // Give chip time to respond if in Channel Pin Mode
  
  uint16_t adc_high = analogRead(RSSI_INPUT_PIN);
  uint8_t rssi_high = (adc_high > 2047 ? 2047 : adc_high) >> 3;
  Serial.printf("   RSSI with pins HIGH: %d (ADC: %d)\n", rssi_high, adc_high);
  Serial.println("   → If generator on 5725 MHz, RSSI should be high\n");
  
  // Analyze results
  Serial.println("═══════════════════════════════════════════════════════════════");
  Serial.println("RESULT ANALYSIS:");
  Serial.println("═══════════════════════════════════════════════════════════════\n");
  
  // Calculate differences
  int diff_low = abs(rssi_low - rssi_spi);
  int diff_high = abs(rssi_high - rssi_spi);
  int diff_between = abs(rssi_high - rssi_low);
  
  Serial.printf("SPI command (5658 MHz):  RSSI = %d\n", rssi_spi);
  Serial.printf("Pins LOW (5865 MHz):     RSSI = %d  (diff from SPI: %d)\n", rssi_low, diff_low);
  Serial.printf("Pins HIGH (5725 MHz):    RSSI = %d  (diff from SPI: %d)\n", rssi_high, diff_high);
  Serial.printf("LOW vs HIGH difference:  %d\n\n", diff_between);
  
  // Determine if in channel pin mode
  bool in_pin_mode = (diff_low > 15 || diff_high > 15 || diff_between > 15);
  
  if (in_pin_mode) {
    Serial.println("❌ RSSI VARIES WITH PIN STATES!");
    Serial.println("\nDIAGNOSIS: RTC6715 is in CHANNEL PIN MODE");
    Serial.println("══════════════════════════════════════════════════════════════");
    Serial.println("The chip is IGNORING SPI commands and using pin voltage");
    Serial.println("levels to select channels.\n");
    
    Serial.println("CONFIRMED PROBLEMS:");
    Serial.println("  ✗ SPI_EN pin is NOT at 3.3V (despite schematic)");
    Serial.println("  ✗ All SPI commands from firmware are ignored");
    Serial.println("  ✗ Frequency is controlled by CH1/CH2/CH3 pin states");
    Serial.println("  ✗ This explains why freq changes don't work!\n");
    
    Serial.println("LIKELY CAUSES:");
    Serial.println("  • Manufacturing defect (trace not connected)");
    Serial.println("  • Cold solder joint on SPI_EN pin");
    Serial.println("  • Wrong component variant (defaults to pin mode)");
    Serial.println("  • PCB design error (schematic shows 3.3V but not routed)\n");
    
    Serial.println("IMMEDIATE ACTION REQUIRED:");
    Serial.println("  1. Power off the board");
    Serial.println("  2. Use multimeter to measure SPI_EN pin voltage");
    Serial.println("     - Should read 3.3V (relative to GND)");
    Serial.println("     - Likely reads 0V or floating");
    Serial.println("  3. Check continuity from SPI_EN to 3.3V rail");
    Serial.println("  4. Fix the connection:");
    Serial.println("     a) Add jumper wire from SPI_EN to 3.3V");
    Serial.println("     b) Or reflow solder if cold joint");
    Serial.println("     c) Or fix broken trace under microscope\n");
    
    Serial.println("CHANNEL PIN MODE FREQUENCY TABLE:");
    Serial.println("  000 (all LOW)  = 5865 MHz (A1)");
    Serial.println("  001            = 5845 MHz (A2)");
    Serial.println("  010            = 5825 MHz (A3)");
    Serial.println("  011            = 5805 MHz (A4)");
    Serial.println("  100            = 5785 MHz (A5)");
    Serial.println("  101            = 5765 MHz (A6)");
    Serial.println("  110            = 5745 MHz (A7)");
    Serial.println("  111 (all HIGH) = 5725 MHz (A8)");
    Serial.println("\n  Your board is likely floating at one of these frequencies");
    Serial.println("  depending on pull-up/down resistors on CH1/CH2/CH3.\n");
    
  } else {
    Serial.println("✓ RSSI STABLE REGARDLESS OF PIN STATES");
    Serial.println("\nDIAGNOSIS: RTC6715 is in SPI MODE (Hardware correct!)");
    Serial.println("══════════════════════════════════════════════════════════════");
    Serial.println("The chip correctly ignores pin states and listens to SPI.\n");
    
    Serial.println("HARDWARE STATUS:");
    Serial.println("  ✓ SPI_EN is at 3.3V (correct)");
    Serial.println("  ✓ Chip is in SPI mode (correct)");
    Serial.println("  ✓ Pin states are being ignored (correct)\n");
    
    Serial.println("But frequency changes still don't work, so the problem is:");
    Serial.println("  ✗ Wrong SPI protocol or timing");
    Serial.println("  ✗ Wrong frequency calculation formula");
    Serial.println("  ✗ Wrong register layout");
    Serial.println("  ✗ Missing initialization sequence\n");
    
    Serial.println("CHECK DATASHEET FOR:");
    Serial.println("  1. Frequency formula - current uses: N=(f-479)/64, A=((f-479)%64)/2");
    Serial.println("  2. Register layout - current uses 25-bit: 4-bit addr + 1 write + 16 data + 4 pad");
    Serial.println("  3. Bit order - current sends LSB first");
    Serial.println("  4. Initialization - check power-up sequence");
    Serial.println("  5. SPI timing - current uses 300µs delays\n");
  }
  
  Serial.println("═══════════════════════════════════════════════════════════════\n");
  
  // Restore pins to normal SPI operation
  Serial.println("Step 3: Restoring normal SPI operation...");
  setupRX5808();
  delay(100);
  Serial.println("   Test complete. Pins restored.\n");
}

void TimingCore::testChannelPinModeLow() {
  Serial.println("\n╔════════════════════════════════════════════════════════════════╗");
  Serial.println("║       RTC6715 CHANNEL PIN MODE TEST - PINS LOW                ║");
  Serial.println("╚════════════════════════════════════════════════════════════════╝\n");
  
  Serial.println("Testing with all pins LOW (forces 5865 MHz in pin mode)...\n");
  
  pinMode(RX5808_DATA_PIN, OUTPUT);
  pinMode(RX5808_CLK_PIN, OUTPUT);
  pinMode(RX5808_SEL_PIN, OUTPUT);
  
  digitalWrite(RX5808_DATA_PIN, LOW);   // CH1 = 0
  digitalWrite(RX5808_CLK_PIN, LOW);    // CH3 = 0
  digitalWrite(RX5808_SEL_PIN, LOW);    // CH2 = 0
  
  Serial.println("Pin States:");
  Serial.println("  DATA (CH1) = LOW");
  Serial.println("  CLK  (CH3) = LOW");
  Serial.println("  SEL  (CH2) = LOW");
  Serial.println("\nIn Channel Pin Mode: 000 = 5865 MHz (Boscam A1)");
  Serial.println("In SPI Mode: Pins ignored, stays at SPI-programmed frequency\n");
  
  Serial.println("Waiting for chip to respond...");
  delay(150);  // Give chip time to respond
  
  uint16_t adc_val = analogRead(RSSI_INPUT_PIN);
  uint8_t rssi = (adc_val > 2047 ? 2047 : adc_val) >> 3;
  
  Serial.println("\n═══════════════════════════════════════════════════════════════");
  Serial.println("RESULT:");
  Serial.println("═══════════════════════════════════════════════════════════════\n");
  Serial.printf("RSSI = %d (ADC: %d)\n\n", rssi, adc_val);
  
  Serial.println("INTERPRETATION:");
  Serial.println("  • If generator on 5865 MHz and RSSI is HIGH (>100):");
  Serial.println("    → Chip is in CHANNEL PIN MODE ❌");
  Serial.println("  • If generator on 5865 MHz and RSSI is LOW (<50):");
  Serial.println("    → Chip is in SPI MODE ✓");
  Serial.println("  • Test with different generator frequencies to confirm\n");
  
  Serial.println("═══════════════════════════════════════════════════════════════\n");
  
  // Keep pins in this state - don't restore
  Serial.println("Pins remain LOW. Change generator frequency to test.\n");
}

void TimingCore::testChannelPinModeHigh() {
  Serial.println("\n╔════════════════════════════════════════════════════════════════╗");
  Serial.println("║       RTC6715 CHANNEL PIN MODE TEST - PINS HIGH               ║");
  Serial.println("╚════════════════════════════════════════════════════════════════╝\n");
  
  Serial.println("Testing with all pins HIGH (forces 5725 MHz in pin mode)...\n");
  
  pinMode(RX5808_DATA_PIN, OUTPUT);
  pinMode(RX5808_CLK_PIN, OUTPUT);
  pinMode(RX5808_SEL_PIN, OUTPUT);
  
  digitalWrite(RX5808_DATA_PIN, HIGH);  // CH1 = 1
  digitalWrite(RX5808_CLK_PIN, HIGH);   // CH3 = 1
  digitalWrite(RX5808_SEL_PIN, HIGH);   // CH2 = 1
  
  Serial.println("Pin States:");
  Serial.println("  DATA (CH1) = HIGH");
  Serial.println("  CLK  (CH3) = HIGH");
  Serial.println("  SEL  (CH2) = HIGH");
  Serial.println("\nIn Channel Pin Mode: 111 = 5725 MHz (Boscam A8)");
  Serial.println("In SPI Mode: Pins ignored, stays at SPI-programmed frequency\n");
  
  Serial.println("Waiting for chip to respond...");
  delay(150);  // Give chip time to respond
  
  uint16_t adc_val = analogRead(RSSI_INPUT_PIN);
  uint8_t rssi = (adc_val > 2047 ? 2047 : adc_val) >> 3;
  
  Serial.println("\n═══════════════════════════════════════════════════════════════");
  Serial.println("RESULT:");
  Serial.println("═══════════════════════════════════════════════════════════════\n");
  Serial.printf("RSSI = %d (ADC: %d)\n\n", rssi, adc_val);
  
  Serial.println("INTERPRETATION:");
  Serial.println("  • If generator on 5725 MHz and RSSI is HIGH (>100):");
  Serial.println("    → Chip is in CHANNEL PIN MODE ❌");
  Serial.println("  • If generator on 5725 MHz and RSSI is LOW (<50):");
  Serial.println("    → Chip is in SPI MODE ✓");
  Serial.println("  • Test with different generator frequencies to confirm\n");
  
  Serial.println("═══════════════════════════════════════════════════════════════\n");
  
  // Keep pins in this state - don't restore
  Serial.println("Pins remain HIGH. Change generator frequency to test.\n");
}

