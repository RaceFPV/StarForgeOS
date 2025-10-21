#include <Arduino.h>
#include "config.h"
#include "timing_core.h"
#include "standalone_mode.h"
#include "node_mode.h"
#include <WiFi.h>

// Global firmware version strings (for compatibility with RotorHazard server)
const char *firmwareVersionString = "FIRMWARE_VERSION: ESP32-C3_Lite_1.0.0";
const char *firmwareBuildDateString = "FIRMWARE_BUILDDATE: " __DATE__;
const char *firmwareBuildTimeString = "FIRMWARE_BUILDTIME: " __TIME__;
const char *firmwareProcTypeString = "FIRMWARE_PROCTYPE: ESP32-C3";

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
  delay(50);  // Brief delay for USB to stabilize
  
  // Initialize mode selection pin (floating=Node, GND=WiFi, HIGH=Node)
  pinMode(MODE_SWITCH_PIN, INPUT_PULLUP);
  
  // TEMPORARY: Force RotorHazard node mode for testing
  // Determine initial mode BEFORE any serial output
  // bool initial_switch_state = digitalRead(MODE_SWITCH_PIN);
  // current_mode = (initial_switch_state == LOW) ? MODE_STANDALONE : MODE_ROTORHAZARD;
  current_mode = MODE_ROTORHAZARD;  // FORCE NODE MODE FOR TESTING
  
  // Only show startup messages in standalone mode (safe for debug output)
  if (current_mode == MODE_STANDALONE) {
    Serial.println();
    Serial.println("=== Tracer ESP32-C3 Timer ===");
    Serial.println("Version: 1.0.0");
    Serial.println("Single-core RISC-V processor");
    Serial.println();
    Serial.println("Mode: STANDALONE/WIFI (Pin 0 = LOW/GND)");
    Serial.println("Initializing timing core...");
    WiFi.softAP("TRACER", ""); // WE NEED THIS HERE FOR SOME DUMB REASON, OTHERWISE THE WIFI DOESN'T START UP CORRECTLY
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
    // LOW (GND) = WiFi mode, HIGH (floating/pullup) = RotorHazard mode (default)
    OperationMode new_mode;
    if (current_switch_state == LOW) {
      new_mode = MODE_STANDALONE;  // Switch to GND = WiFi mode
    } else {
      new_mode = MODE_ROTORHAZARD; // Switch to 3.3V or floating = RotorHazard mode (default)
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
    
    // Shutdown standalone mode if it was running  
    if (mode_initialized) {
      // Standalone mode doesn't need explicit shutdown
    }
    
    // Initialize node mode (silently)
    node.begin(&timing);
    
    // Node mode is now active and waiting for RotorHazard commands
    // All communication is binary - no text output allowed
  }
  
  mode_initialized = true;
}
