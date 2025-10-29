/*
 * Receiver Test - Minimal RTC6715 Diagnostic Tool
 * 
 * Ultra-lightweight test firmware for verifying RTC6715 functionality
 * Serial console only - no WiFi, no web server, no timing system
 * 
 * Commands:
 *   f <freq>  - Set frequency (e.g., "f 5800") [disables auto-cycle]
 *   a         - Toggle auto-cycle between 5725/5800 (10 sec interval)
 *   r         - Read current RSSI
 *   i         - Initialize/reset RTC6715
 *   c         - Force channel mode test (pins LOW, stays LOW)
 *   n         - Restore normal operation (after 'c' command)
 *   s         - Show current status
 *   h         - Show help
 * 
 * Default: Auto-cycles between 5725 and 5800 MHz every 10 seconds
 */

#include <Arduino.h>

// Pin definitions (same as NovaCore board)
#define RSSI_INPUT_PIN      3     // ADC1_CH3 - RSSI input from RX5808
#define RX5808_DATA_PIN     6     // SPI MOSI to RX5808 module
#define RX5808_CLK_PIN      4     // SPI SCK to RX5808 module
#define RX5808_SEL_PIN      7     // SPI CS to RX5808 module

// RX5808 timing constants
#define RX5808_MIN_TUNETIME 35    // ms - wait after freq change

// Global state
uint16_t current_frequency = 5725;  // Start with 5725
uint32_t last_freq_change = 0;
bool freq_recently_changed = false;

// Auto-cycling state
bool auto_cycle_enabled = true;
uint32_t last_cycle_time = 0;
const uint32_t CYCLE_INTERVAL = 10000;  // 10 seconds
bool cycle_on_5725 = true;  // Start with 5725

// Function prototypes
void sendRX5808Bit(uint8_t bit_value);
void resetRX5808Module();
void configureRX5808Power();
void setFrequency(uint16_t freq_mhz);
uint8_t readRSSI();
void processCommand(String cmd);
void showHelp();
void showStatus();
void forceChannelModeTest();
void restoreNormalOperation();

void setup() {
  Serial.begin(921600);
  delay(100);
  
  Serial.println("\n\n");
  Serial.println("╔════════════════════════════════════════════════════╗");
  Serial.println("║      RECEIVER TEST - RTC6715 Diagnostic Tool       ║");
  Serial.println("╚════════════════════════════════════════════════════╝");
  Serial.println();
  Serial.println("Minimal firmware for testing RTC6715 chips");
  Serial.println("Type 'h' for help\n");
  
  // Initialize pins
  pinMode(RSSI_INPUT_PIN, INPUT);
  pinMode(RX5808_DATA_PIN, OUTPUT);
  pinMode(RX5808_CLK_PIN, OUTPUT);
  pinMode(RX5808_SEL_PIN, OUTPUT);
  
  // Set initial states
  digitalWrite(RX5808_SEL_PIN, HIGH);
  digitalWrite(RX5808_CLK_PIN, LOW);
  digitalWrite(RX5808_DATA_PIN, LOW);
  
  // Configure ADC for full 0-3.3V range
  analogSetAttenuation(ADC_11db);
  
  Serial.println("Initializing RTC6715...");
  delay(100);
  
  // Reset and configure module
  resetRX5808Module();
  configureRX5808Power();
  
  // Set default frequency
  setFrequency(current_frequency);
  
  Serial.println("✓ Initialization complete\n");
  Serial.println("╔════════════════════════════════════════════════════╗");
  Serial.println("║           AUTO-CYCLE MODE: ACTIVE                 ║");
  Serial.println("╚════════════════════════════════════════════════════╝");
  Serial.println("→ Cycling between 5725 MHz ↔ 5800 MHz every 10 sec");
  Serial.println("→ Set generator to 5725 or 5800 and watch RSSI");
  Serial.println("→ Type 'a' to disable, 'f <freq>' to set manual freq");
  Serial.println();
  showStatus();
  Serial.println();
  showHelp();
}

void loop() {
  // Check for serial commands
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() > 0) {
      processCommand(cmd);
    }
  }
  
  // Auto-cycle between 5725 and 5800 if enabled
  if (auto_cycle_enabled && (millis() - last_cycle_time > CYCLE_INTERVAL)) {
    if (cycle_on_5725) {
      Serial.println("\n→ AUTO-CYCLE: Switching to 5800 MHz");
      setFrequency(5800);
      cycle_on_5725 = false;
    } else {
      Serial.println("\n→ AUTO-CYCLE: Switching to 5725 MHz");
      setFrequency(5725);
      cycle_on_5725 = true;
    }
    last_cycle_time = millis();
    Serial.println();
  }
  
  // Auto-display RSSI every second
  static uint32_t last_display = 0;
  if (millis() - last_display > 1000) {
    uint8_t rssi = readRSSI();
    if (auto_cycle_enabled) {
      Serial.printf("[RSSI] %d | Freq: %d MHz | AUTO-CYCLE: %s\n", 
                    rssi, current_frequency, 
                    cycle_on_5725 ? "5725 MHz" : "5800 MHz");
    } else {
      Serial.printf("[RSSI] %d | Freq: %d MHz\n", rssi, current_frequency);
    }
    last_display = millis();
  }
  
  delay(10);
}

void processCommand(String cmd) {
  cmd.toLowerCase();
  
  if (cmd.startsWith("f ")) {
    // Set frequency command
    int freq = cmd.substring(2).toInt();
    if (freq >= 5645 && freq <= 5945) {
      // Disable auto-cycling when user manually sets frequency
      if (auto_cycle_enabled) {
        auto_cycle_enabled = false;
        Serial.println("\n→ AUTO-CYCLE DISABLED (manual freq set)");
      }
      Serial.printf("→ Setting frequency to %d MHz...\n", freq);
      setFrequency(freq);
      delay(50);
      uint8_t rssi = readRSSI();
      Serial.printf("✓ Frequency set. Current RSSI: %d\n\n", rssi);
    } else {
      Serial.println("✗ Invalid frequency. Range: 5645-5945 MHz\n");
    }
  }
  else if (cmd == "a") {
    // Re-enable auto-cycling
    auto_cycle_enabled = !auto_cycle_enabled;
    if (auto_cycle_enabled) {
      Serial.println("\n✓ AUTO-CYCLE ENABLED");
      Serial.println("  Will cycle between 5725 and 5800 every 10 seconds\n");
      last_cycle_time = millis();  // Reset timer
    } else {
      Serial.println("\n✓ AUTO-CYCLE DISABLED\n");
    }
  }
  else if (cmd == "r") {
    // Read RSSI command
    uint8_t rssi = readRSSI();
    uint16_t adc = analogRead(RSSI_INPUT_PIN);
    Serial.printf("\n[RSSI Reading]\n");
    Serial.printf("  RSSI: %d (0-255)\n", rssi);
    Serial.printf("  ADC:  %d (0-4095)\n", adc);
    Serial.printf("  Freq: %d MHz\n\n", current_frequency);
  }
  else if (cmd == "i") {
    // Initialize/reset command
    Serial.println("\n→ Resetting RTC6715...");
    resetRX5808Module();
    configureRX5808Power();
    setFrequency(current_frequency);
    Serial.println("✓ Reset complete\n");
  }
  else if (cmd == "c") {
    // Channel mode test command
    forceChannelModeTest();
  }
  else if (cmd == "n") {
    // Restore normal operation command
    restoreNormalOperation();
  }
  else if (cmd == "s") {
    // Status command
    Serial.println();
    showStatus();
    Serial.println();
  }
  else if (cmd == "h") {
    // Help command
    Serial.println();
    showHelp();
  }
  else {
    Serial.printf("✗ Unknown command: '%s'\n", cmd.c_str());
    Serial.println("Type 'h' for help\n");
  }
}

void showHelp() {
  Serial.println("═══════════════════════════════════════════════════");
  Serial.println("COMMANDS:");
  Serial.println("═══════════════════════════════════════════════════");
  Serial.println("  f <freq>  Set frequency in MHz (5645-5945)");
  Serial.println("            Example: f 5800");
  Serial.println("            (Disables auto-cycle)");
  Serial.println();
  Serial.println("  a         Toggle AUTO-CYCLE mode");
  Serial.println("            Cycles between 5725/5800 every 10 sec");
  Serial.println();
  Serial.println("  r         Read current RSSI value");
  Serial.println("  i         Initialize/reset RTC6715 chip");
  Serial.println("  c         Force channel mode test");
  Serial.println("            (Sets pins LOW, STAYS LOW)");
  Serial.println("  n         Restore normal operation");
  Serial.println("            (Use after 'c' command)");
  Serial.println("  s         Show current status");
  Serial.println("  h         Show this help");
  Serial.println("═══════════════════════════════════════════════════");
  Serial.println();
  Serial.println("AUTO-CYCLE MODE (Default: ON):");
  Serial.println("  → Automatically cycles between 5725 and 5800 MHz");
  Serial.println("  → 10 second intervals");
  Serial.println("  → Perfect for quick board testing");
  Serial.println("  → Just set generator to 5725 or 5800 and watch RSSI");
  Serial.println();
  Serial.println("MANUAL TEST PROCEDURE:");
  Serial.println("  1. Set generator to known frequency (e.g., 5800)");
  Serial.println("  2. Type: f 5800");
  Serial.println("  3. Verify RSSI is HIGH (>100)");
  Serial.println("  4. Type: f 5658");
  Serial.println("  5. Verify RSSI drops LOW (<50)");
  Serial.println("  6. If RSSI doesn't change → chip in channel mode!");
  Serial.println("═══════════════════════════════════════════════════");
}

void showStatus() {
  Serial.println("═══════════════════════════════════════════════════");
  Serial.println("CURRENT STATUS:");
  Serial.println("═══════════════════════════════════════════════════");
  Serial.printf("  Frequency:  %d MHz\n", current_frequency);
  Serial.printf("  RSSI:       %d\n", readRSSI());
  Serial.printf("  Auto-cycle: %s\n", auto_cycle_enabled ? "ENABLED (5725↔5800)" : "DISABLED");
  Serial.printf("  Uptime:     %lu seconds\n", millis() / 1000);
  Serial.println();
  Serial.println("  Pin Configuration:");
  Serial.printf("    RSSI:  GPIO %d (ADC)\n", RSSI_INPUT_PIN);
  Serial.printf("    DATA:  GPIO %d\n", RX5808_DATA_PIN);
  Serial.printf("    CLK:   GPIO %d\n", RX5808_CLK_PIN);
  Serial.printf("    SEL:   GPIO %d\n", RX5808_SEL_PIN);
  Serial.println("═══════════════════════════════════════════════════");
}

uint8_t readRSSI() {
  // Check if frequency was recently changed
  if (freq_recently_changed) {
    uint32_t time_since_change = millis() - last_freq_change;
    if (time_since_change < RX5808_MIN_TUNETIME) {
      delay(RX5808_MIN_TUNETIME - time_since_change);
    }
    freq_recently_changed = false;
  }
  
  // Read 12-bit ADC value
  uint16_t adc_value = analogRead(RSSI_INPUT_PIN);
  
  // Clamp to 2047 (RX5808 typically outputs 0-2V, not full 3.3V)
  if (adc_value > 2047) {
    adc_value = 2047;
  }
  
  // Scale to 0-255
  return adc_value >> 3;
}

void setFrequency(uint16_t freq_mhz) {
  if (freq_mhz < 5645 || freq_mhz > 5945) {
    Serial.printf("ERROR: Invalid frequency %d MHz\n", freq_mhz);
    return;
  }
  
  // Calculate register value (standard RX5808 formula)
  uint16_t tf = (freq_mhz - 479) / 2;
  uint16_t N = tf / 32;
  uint16_t A = tf % 32;
  uint16_t vtxHex = (N << 7) + A;
  
  Serial.printf("  Formula: tf=%d, N=%d, A=%d, reg=0x%04X\n", tf, N, A, vtxHex);
  Serial.print("  Sending SPI bits: 0001 1 ");
  
  // Send 25-bit SPI command
  digitalWrite(RX5808_SEL_PIN, HIGH);
  digitalWrite(RX5808_SEL_PIN, LOW);
  
  // Register 0x1 (frequency register) - LSB first
  sendRX5808Bit(1);  // 0001 in LSB first order
  sendRX5808Bit(0);
  sendRX5808Bit(0);
  sendRX5808Bit(0);
  
  // Write flag
  sendRX5808Bit(1);
  
  // Data bits D0-D15 (LSB first)
  for (uint8_t i = 0; i < 16; i++) {
    uint8_t bit = (vtxHex >> i) & 0x1;
    sendRX5808Bit(bit);
    Serial.print(bit);
    if (i % 4 == 3) Serial.print(" ");
  }
  
  // Padding bits D16-D19
  sendRX5808Bit(0);
  sendRX5808Bit(0);
  sendRX5808Bit(0);
  sendRX5808Bit(0);
  Serial.println("0000");
  
  digitalWrite(RX5808_SEL_PIN, HIGH);
  delay(2);
  
  digitalWrite(RX5808_CLK_PIN, LOW);
  digitalWrite(RX5808_DATA_PIN, LOW);
  
  // Update state
  current_frequency = freq_mhz;
  freq_recently_changed = true;
  last_freq_change = millis();
  
  Serial.println("  SPI command sent");
}

void sendRX5808Bit(uint8_t bit_value) {
  digitalWrite(RX5808_DATA_PIN, bit_value ? HIGH : LOW);
  delayMicroseconds(300);
  
  digitalWrite(RX5808_CLK_PIN, HIGH);
  delayMicroseconds(300);
  
  digitalWrite(RX5808_CLK_PIN, LOW);
  delayMicroseconds(300);
}

void resetRX5808Module() {
  Serial.println("  Sending reset command (register 0xF)...");
  
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
  
  Serial.println("  Reset complete");
}

void configureRX5808Power() {
  Serial.println("  Configuring power register (0xA)...");
  
  digitalWrite(RX5808_SEL_PIN, HIGH);
  digitalWrite(RX5808_SEL_PIN, LOW);
  
  // Register 0xA (power register) = 1010
  sendRX5808Bit(0);
  sendRX5808Bit(1);
  sendRX5808Bit(0);
  sendRX5808Bit(1);
  
  // Write flag
  sendRX5808Bit(1);
  
  // 20 bits of power configuration: 0b11010000110111110011
  uint32_t power_config = 0b11010000110111110011;
  for (uint8_t i = 0; i < 20; i++) {
    sendRX5808Bit((power_config >> i) & 0x1);
  }
  
  digitalWrite(RX5808_SEL_PIN, HIGH);
  delay(10);
  
  digitalWrite(RX5808_DATA_PIN, LOW);
  
  Serial.println("  Power configuration complete");
}

void forceChannelModeTest() {
  Serial.println("\n╔════════════════════════════════════════════════════╗");
  Serial.println("║      CHANNEL PIN MODE TEST - FORCE TO 000         ║");
  Serial.println("╚════════════════════════════════════════════════════╝\n");
  
  Serial.println("This test uses RTC6715 State Register (0x0F)");
  Serial.println("to trigger RESET state while pins are LOW.");
  Serial.println("If chip is in Channel Pin Mode, it will re-sample");
  Serial.println("pins and latch to: CH1=0, CH2=0, CH3=0 = 5865 MHz\n");
  
  // Read RSSI before
  uint8_t rssi_before = readRSSI();
  Serial.printf("RSSI before test: %d\n\n", rssi_before);
  
  // Force all pins LOW
  Serial.println("→ Setting all three SPI pins LOW...");
  pinMode(RX5808_DATA_PIN, OUTPUT);
  pinMode(RX5808_CLK_PIN, OUTPUT);
  pinMode(RX5808_SEL_PIN, OUTPUT);
  
  digitalWrite(RX5808_DATA_PIN, LOW);   // CH1 = 0
  digitalWrite(RX5808_CLK_PIN, LOW);    // CH3 = 0
  digitalWrite(RX5808_SEL_PIN, LOW);    // CH2 = 0
  
  Serial.println("  DATA (CH1): LOW");
  Serial.println("  CLK  (CH3): LOW");
  Serial.println("  SEL  (CH2): LOW");
  delay(10);
  
  // Now send SPI command to address 0x0F (State Register) to trigger RESET
  Serial.println("\n→ Sending SPI command to State Register (0x0F)...");
  Serial.println("  Writing state = 000 (RESET)");
  Serial.print("  SPI bits: 1111 1 000");
  
  // Start SPI transaction
  digitalWrite(RX5808_SEL_PIN, LOW);
  delayMicroseconds(100);
  
  // Register 0x0F (state register) - LSB first: 1111
  sendRX5808Bit(1);
  sendRX5808Bit(1);
  sendRX5808Bit(1);
  sendRX5808Bit(1);
  
  // Write flag
  sendRX5808Bit(1);
  
  // State value: 000 (RESET) in bits D0-D2, rest zeros
  sendRX5808Bit(0);  // D0
  sendRX5808Bit(0);  // D1
  sendRX5808Bit(0);  // D2
  
  // Remaining data bits D3-D19 (all zeros)
  for (uint8_t i = 3; i < 20; i++) {
    sendRX5808Bit(0);
    if (i == 2 || i == 6 || i == 10 || i == 14 || i == 18) Serial.print(" ");
  }
  Serial.println();
  
  // End SPI transaction
  digitalWrite(RX5808_SEL_PIN, HIGH);
  delayMicroseconds(100);
  
  // Keep pins LOW after command
  digitalWrite(RX5808_CLK_PIN, LOW);
  digitalWrite(RX5808_DATA_PIN, LOW);
  
  Serial.println("  RESET command sent");
  
  // Wait for chip to reset and stabilize
  Serial.println("\n→ Waiting for chip to reset and stabilize (200ms)...");
  delay(200);
  
  // Read RSSI after
  uint8_t rssi_after = readRSSI();
  Serial.printf("\nRSSI after test:  %d\n", rssi_after);
  Serial.printf("RSSI difference:  %d\n\n", abs(rssi_after - rssi_before));
  
  // Analyze results
  Serial.println("═══════════════════════════════════════════════════");
  Serial.println("RESULT ANALYSIS:");
  Serial.println("═══════════════════════════════════════════════════");
  
  if (abs(rssi_after - rssi_before) > 15) {
    Serial.println("\n✗ RSSI CHANGED SIGNIFICANTLY!");
    Serial.println("\nDIAGNOSIS: Chip is in CHANNEL PIN MODE");
    Serial.println("  → Chip reset and re-sampled pin states");
    Serial.println("  → Now locked to 5865 MHz (000 = A1 channel)");
    Serial.println("  → SPI_SE pin not enabling SPI mode");
    Serial.println("\nWith generator on 5865 MHz, RSSI should be HIGH");
  } else {
    Serial.println("\n✓ RSSI REMAINED STABLE");
    Serial.println("\nDIAGNOSIS: Could mean several things:");
    Serial.println("  A) Chip is in SPI MODE (ignored pin states - GOOD!)");
    Serial.println("  B) Chip ignored state register command (still in channel mode)");
    Serial.println("  C) Already was at 5865 MHz by chance");
    Serial.println("\nTry: Set generator to 5865 MHz and read RSSI");
    Serial.println("     If RSSI is HIGH → chip is at 5865 (channel mode)");
    Serial.println("     If RSSI is LOW → chip ignored command");
  }
  
  Serial.println("═══════════════════════════════════════════════════");
  
  Serial.println("\n→ PINS REMAIN LOW - Use generator to test");
  Serial.println("  Command 'r' will read current RSSI");
  Serial.println("  Command 'n' will restore normal operation\n");
}

void restoreNormalOperation() {
  Serial.println("\n╔════════════════════════════════════════════════════╗");
  Serial.println("║           RESTORE NORMAL OPERATION                ║");
  Serial.println("╚════════════════════════════════════════════════════╝\n");
  
  Serial.println("→ Restoring normal pin configuration...");
  digitalWrite(RX5808_SEL_PIN, HIGH);
  digitalWrite(RX5808_CLK_PIN, LOW);
  digitalWrite(RX5808_DATA_PIN, LOW);
  
  Serial.println("  DATA: LOW");
  Serial.println("  CLK:  LOW");
  Serial.println("  SEL:  HIGH");
  
  // Do a proper reset and init
  Serial.println("\n→ Performing full chip re-initialization...");
  resetRX5808Module();
  configureRX5808Power();
  setFrequency(current_frequency);
  
  Serial.printf("\n✓ Normal operation restored to %d MHz\n\n", current_frequency);
}

