#!/usr/bin/env python3
"""
Silent RotorHazard Mode Test
Tests that RotorHazard mode produces NO stray Serial output

CRITICAL: Any debug messages break the RotorHazard binary protocol!

This test:
1. Connects to the node
2. Waits for boot to complete
3. Sends protocol commands
4. Checks that responses are ONLY protocol messages (no debug text)
5. Validates no "Serial.println" style output appears

Usage:
    python3 test_silent_rotorhazard.py /dev/ttyUSB0
    python3 test_silent_rotorhazard.py COM3 (Windows)
"""

import serial
import time
import sys
import re

# Protocol commands (must match node_mode.cpp)
READ_ADDRESS = 0x00
READ_FREQUENCY = 0x03
READ_ENTER_AT_LEVEL = 0x31
READ_EXIT_AT_LEVEL = 0x32
WRITE_FREQUENCY = 0x51

NODE_API_LEVEL = 35

class SilentModeTest:
    def __init__(self, port: str, baudrate: int = 921600):
        self.port = port
        self.baudrate = baudrate
        self.ser = None
        self.test_passed = True
        self.violations = []
        
    def connect(self):
        """Connect to node"""
        try:
            self.ser = serial.Serial(
                self.port,
                self.baudrate,
                timeout=2.0
            )
            print(f"✓ Connected to {self.port}")
            return True
        except Exception as e:
            print(f"✗ Failed to connect: {e}")
            return False
    
    def wait_for_boot(self):
        """Wait for ESP32 to boot and clear boot messages"""
        print("⏳ Waiting for boot to complete...")
        time.sleep(3)
        
        # Flush any boot messages from ROM bootloader
        self.ser.reset_input_buffer()
        time.sleep(0.5)
        
        # Read any remaining data
        boot_data = self.ser.read(self.ser.in_waiting)
        if boot_data:
            print(f"📝 Boot messages ({len(boot_data)} bytes) - OK (expected)")
            # Boot messages are OK, we're testing POST-boot silence
        
        # Clear buffer again
        self.ser.reset_input_buffer()
        print("✓ Boot complete, buffer cleared")
    
    def check_for_text_output(self, data: bytes, context: str) -> bool:
        """Check if data contains ASCII text (debug output)"""
        if not data:
            return False
        
        # Check for common debug patterns
        debug_patterns = [
            b'ConfigLoader:',
            b'Mode:',
            b'STANDALONE',
            b'ROTORHAZARD',
            b'Default mode',
            b'Touch board',
            b'Mode switch',
            b'Board:',
            b'Pin Config:',
            b'GPIO',
            b'===',
            b'Initializing',
            b'StarForge',
            b'Version:',
        ]
        
        for pattern in debug_patterns:
            if pattern in data:
                violation = f"Found debug text '{pattern.decode('ascii', errors='ignore')}' in {context}"
                self.violations.append(violation)
                print(f"✗ {violation}")
                self.test_passed = False
                return True
        
        # Check for high percentage of printable ASCII
        printable_count = sum(1 for b in data if 0x20 <= b <= 0x7E or b in [0x0A, 0x0D])
        if len(data) > 10 and printable_count > len(data) * 0.7:
            # More than 70% printable = probably text
            violation = f"High ASCII content in {context}: {data[:50]}"
            self.violations.append(violation)
            print(f"✗ {violation}")
            self.test_passed = False
            return True
        
        return False
    
    def test_silent_read_address(self):
        """Test: READ_ADDRESS should return only API level byte"""
        print("\n[TEST] READ_ADDRESS (should return 1 byte, no debug)")
        
        # Clear buffer
        self.ser.reset_input_buffer()
        
        # Send command
        self.ser.write(bytes([READ_ADDRESS]))
        self.ser.flush()
        time.sleep(0.1)
        
        # Read response
        response = self.ser.read(self.ser.in_waiting)
        
        if len(response) == 0:
            print("✗ No response")
            self.test_passed = False
        elif len(response) == 1:
            if response[0] == NODE_API_LEVEL:
                print(f"✓ Got API level: {response[0]} (correct)")
            else:
                print(f"⚠ Got unexpected API level: {response[0]} (expected {NODE_API_LEVEL})")
        else:
            print(f"⚠ Got {len(response)} bytes (expected 1)")
            self.check_for_text_output(response, "READ_ADDRESS response")
    
    def test_silent_read_frequency(self):
        """Test: READ_FREQUENCY should return only 2 bytes (frequency)"""
        print("\n[TEST] READ_FREQUENCY (should return 2 bytes, no debug)")
        
        self.ser.reset_input_buffer()
        
        self.ser.write(bytes([READ_FREQUENCY]))
        self.ser.flush()
        time.sleep(0.1)
        
        response = self.ser.read(self.ser.in_waiting)
        
        if len(response) == 0:
            print("✗ No response")
            self.test_passed = False
        elif len(response) == 2:
            freq = (response[0] << 8) | response[1]
            print(f"✓ Got frequency: {freq} MHz (correct)")
        else:
            print(f"⚠ Got {len(response)} bytes (expected 2)")
            self.check_for_text_output(response, "READ_FREQUENCY response")
    
    def test_silent_write_frequency(self):
        """Test: WRITE_FREQUENCY should have no debug output"""
        print("\n[TEST] WRITE_FREQUENCY (should be silent, no response)")
        
        self.ser.reset_input_buffer()
        
        # Write frequency 5800 MHz
        freq = 5800
        freq_high = (freq >> 8) & 0xFF
        freq_low = freq & 0xFF
        checksum = (freq_high + freq_low) & 0xFF
        
        command = bytes([WRITE_FREQUENCY, freq_high, freq_low, checksum])
        self.ser.write(command)
        self.ser.flush()
        time.sleep(0.2)
        
        # Should be NO response
        response = self.ser.read(self.ser.in_waiting)
        
        if len(response) == 0:
            print("✓ No output (correct - write commands don't respond)")
        else:
            print(f"⚠ Got unexpected output ({len(response)} bytes)")
            self.check_for_text_output(response, "WRITE_FREQUENCY output")
    
    def test_idle_silence(self):
        """Test: Node should be silent when idle"""
        print("\n[TEST] Idle Silence (no commands, should be silent)")
        
        self.ser.reset_input_buffer()
        time.sleep(1.0)
        
        response = self.ser.read(self.ser.in_waiting)
        
        if len(response) == 0:
            print("✓ No idle output (correct)")
        else:
            print(f"✗ Got unexpected idle output ({len(response)} bytes)")
            self.check_for_text_output(response, "idle period")
    
    def test_rapid_commands(self):
        """Test: Rapid commands should not trigger debug output"""
        print("\n[TEST] Rapid Commands (should stay silent)")
        
        self.ser.reset_input_buffer()
        
        # Send multiple commands rapidly
        commands = [READ_ADDRESS, READ_FREQUENCY, READ_ENTER_AT_LEVEL, READ_EXIT_AT_LEVEL]
        for cmd in commands:
            self.ser.write(bytes([cmd]))
            time.sleep(0.05)
        
        self.ser.flush()
        time.sleep(0.5)
        
        # Read all responses
        response = self.ser.read(self.ser.in_waiting)
        
        # Should be: 1 + 2 + 1 + 1 = 5 bytes of protocol responses
        expected_bytes = 5
        
        if len(response) <= expected_bytes + 2:  # Allow small tolerance
            print(f"✓ Got {len(response)} bytes (expected ~{expected_bytes})")
        else:
            print(f"⚠ Got {len(response)} bytes (expected ~{expected_bytes})")
            self.check_for_text_output(response, "rapid commands")
    
    def run_all_tests(self):
        """Run complete silent operation test suite"""
        print("\n" + "="*60)
        print("StarForgeOS RotorHazard Silent Mode Test")
        print("="*60)
        
        if not self.connect():
            return False
        
        self.wait_for_boot()
        
        # Run tests
        self.test_silent_read_address()
        self.test_silent_read_frequency()
        self.test_silent_write_frequency()
        self.test_idle_silence()
        self.test_rapid_commands()
        
        # Results
        print("\n" + "="*60)
        if self.test_passed:
            print("✅ ALL TESTS PASSED - RotorHazard mode is SILENT")
            print("="*60)
            return True
        else:
            print("❌ TESTS FAILED - Debug output detected!")
            print("="*60)
            print("\nViolations:")
            for v in self.violations:
                print(f"  - {v}")
            print("\nFIX: Add guards around Serial.print statements:")
            print("  if (current_mode == MODE_STANDALONE) {")
            print("      Serial.println(...);")
            print("  }")
            return False
    
    def disconnect(self):
        """Close connection"""
        if self.ser:
            self.ser.close()
            print("\n✓ Disconnected")


def main():
    if len(sys.argv) < 2:
        print("Usage: python3 test_silent_rotorhazard.py <PORT>")
        print("Example:")
        print("  python3 test_silent_rotorhazard.py /dev/ttyUSB0")
        print("  python3 test_silent_rotorhazard.py COM3")
        sys.exit(1)
    
    port = sys.argv[1]
    
    tester = SilentModeTest(port)
    success = tester.run_all_tests()
    tester.disconnect()
    
    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()

