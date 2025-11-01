#include <Arduino.h>
#include "config.h"
#include "timing_core.h"
#include "standalone_mode.h"
#include "node_mode.h"
#include <WiFi.h>

// Global firmware version strings (for compatibility with RotorHazard server)
const char *firmwareVersionString = "FIRMWARE_VERSION: ESP32_1.0.0";
const char *firmwareBuildDateString = "FIRMWARE_BUILDDATE: " __DATE__;
const char *firmwareBuildTimeString = "FIRMWARE_BUILDTIME: " __TIME__;
const char *firmwareProcTypeString = "FIRMWARE_PROCTYPE: ESP32";

// Global objects
TimingCore timing;
StandaloneMode standalone;
NodeMode node;

// Current operation mode
enum OperationMode {
  MODE_STANDALONE,
  MODE_ROTORHAZARD
};

OperationMode current_mode;
bool mode_initialized = false;

// Function declarations
void checkModeSwitch();
void initializeMode();
void serialEvent();

void setup() {
  Serial.begin(UART_BAUD_RATE);
  delay(500);  // Longer delay to ensure all ESP-IDF boot messages complete
  
  // Clear any bootloader/ESP-IDF messages (ESP32 ROM bootloader + ESP-IDF errors)
  // This prevents garbage data from interfering with RotorHazard node detection
  while (Serial.available()) {
    Serial.read();
  }
  delay(200);  // Allow any remaining boot output to arrive
  while (Serial.available()) {
    Serial.read();
  }
  
  // Additional delay for RotorHazard mode to ensure boot messages are done
  // This prevents boot messages from being misinterpreted as protocol responses
  delay(300);
  
  // Initialize mode selection pin (LOW=Standalone, HIGH/floating=RotorHazard)
  pinMode(MODE_SWITCH_PIN, INPUT_PULLUP);
  
  // Determine initial mode BEFORE any serial output
  // LOW (GND) = Standalone mode, HIGH (floating/pullup) = RotorHazard mode
  bool initial_switch_state = digitalRead(MODE_SWITCH_PIN);
  current_mode = (initial_switch_state == LOW) ? MODE_STANDALONE : MODE_ROTORHAZARD;
  
  // Only show startup messages in standalone mode (safe for debug output)
  if (current_mode == MODE_STANDALONE) {
    Serial.println();
    Serial.println("=== StarForge ESP32 Timer ===");
    Serial.println("Version: 1.0.0");
    Serial.println();
    Serial.println("Mode: STANDALONE/WIFI");
    Serial.println("Initializing timing core...");
    WiFi.softAP("SFOS", ""); // WE NEED THIS HERE FOR SOME DUMB REASON, OTHERWISE THE WIFI DOESN'T START UP CORRECTLY
  }
  
  // Initialize core timing system (creates task but keeps it INACTIVE)
  // On single-core ESP32-C3, timing task must NOT run until after mode initialization
  // Otherwise it starves serial/WiFi setup (same issue as WiFi AP initialization)
  timing.begin();
  
  // Set debug mode based on current mode
  bool debug_mode = (current_mode == MODE_STANDALONE);
  timing.setDebugMode(debug_mode);
  
  // Initialize mode-specific functionality BEFORE activating timing task
  initializeMode();
  
  // NOW activate timing core after mode setup is complete
  // This ensures serial port and WiFi are fully initialized before timing task runs
  timing.setActivated(true);
}

void loop() {
  // Check for mode changes
  checkModeSwitch();
  
  // Always process core timing (now handled by FreeRTOS task)
  timing.process();
  
  // Process mode-specific functions
  if (current_mode == MODE_STANDALONE) {
    standalone.process();
  } else {
    // In node mode, prioritize serial processing
    node.handleSerialInput();  // Direct call for faster response
    node.process();
  }
  
  // Handle serial communication again (belt and suspenders)
  serialEvent();
  
  // Very brief yield to allow timing task to run (ESP32-C3 single core)
  // Must be short to ensure responsive serial communication
  taskYIELD();  // Just yield, don't sleep - faster response
}

// Serial event handler (like Arduino)
void serialEvent() {
  // Only handle serial in RotorHazard node mode
  if (current_mode == MODE_ROTORHAZARD) {
    node.handleSerialInput();
  }
}

void checkModeSwitch() {
  static unsigned long last_check = 0;
  static bool last_switch_state = -1; // Initialize to invalid state to force first check
  
  // Only check every 100ms to avoid bouncing
  if (millis() - last_check < 100) {
    return;
  }
  
  bool current_switch_state = digitalRead(MODE_SWITCH_PIN);
  
  if (current_switch_state != last_switch_state) {
    // Determine new mode
    // LOW (GND) = Standalone/WiFi mode, HIGH (floating/pullup) = RotorHazard mode (default)
    OperationMode new_mode;
    if (current_switch_state == LOW) {
      new_mode = MODE_STANDALONE;  // Switch to GND = Standalone mode (LCD active)
    } else {
      new_mode = MODE_ROTORHAZARD; // Switch floating/HIGH = RotorHazard mode
    }
    
    if (new_mode != current_mode || !mode_initialized) {
      current_mode = new_mode;
      // Update debug mode for timing core
      timing.setDebugMode(current_mode == MODE_STANDALONE);
      initializeMode();
    }
    
    last_switch_state = current_switch_state;
  }
  
  last_check = millis();
}

void initializeMode() {
  if (current_mode == MODE_STANDALONE) {
    // Only show debug output in standalone mode
    Serial.println("TimingCore: Ready");
    Serial.println();
    Serial.println("=== WIFI MODE ACTIVE ===");
    
    // Initialize standalone mode
    standalone.begin(&timing);
    
    // The standalone.begin() should output WiFi connection details
    Serial.println("Setup complete!");
    Serial.println();
    
  } else {
    // NODE MODE: NO debug output - it interferes with binary serial protocol
    // Silent initialization for RotorHazard compatibility
    
    // Initialize node mode (silently)
    node.begin(&timing);
    
    // Node mode is now active and waiting for RotorHazard commands
    // All communication is binary - no text output allowed
  }
  
  mode_initialized = true;
}
