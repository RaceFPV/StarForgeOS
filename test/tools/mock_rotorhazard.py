#!/usr/bin/env python3
"""
Mock RotorHazard Server for Testing StarForgeOS Node Mode
Implements the minimal RotorHazard serial protocol for automated testing
"""

import serial
import time
import sys
import struct
from dataclasses import dataclass

# Command constants (must match node_mode.cpp)
READ_ADDRESS = 0x00
READ_FREQUENCY = 0x03
READ_LAP_STATS = 0x05
READ_RHFEAT_FLAGS = 0x11
READ_REVISION_CODE = 0x22
READ_NODE_RSSI_PEAK = 0x23
READ_ENTER_AT_LEVEL = 0x31
READ_EXIT_AT_LEVEL = 0x32
READ_FW_VERSION = 0x3D
READ_FW_BUILDDATE = 0x3E
READ_FW_BUILDTIME = 0x3F
READ_FW_PROCTYPE = 0x40

WRITE_FREQUENCY = 0x51
WRITE_ENTER_AT_LEVEL = 0x71
WRITE_EXIT_AT_LEVEL = 0x72

NODE_API_LEVEL = 35


@dataclass
class NodeState:
    """Track node state for validation"""
    frequency: int = 5800
    enter_at_level: int = 96
    exit_at_level: int = 80
    api_level: int = NODE_API_LEVEL
    

class MockRotorHazardServer:
    def __init__(self, port: str, baudrate: int = 921600, timeout: float = 2.0):
        self.port = port
        self.baudrate = baudrate
        self.timeout = timeout
        self.ser = None
        self.state = NodeState()
        self.test_results = []
        
    def connect(self):
        """Connect to the node via serial"""
        try:
            self.ser = serial.Serial(
                self.port,
                self.baudrate,
                timeout=self.timeout
            )
            time.sleep(2)  # Wait for ESP32 to boot
            print(f"✓ Connected to {self.port} at {self.baudrate} baud")
            return True
        except Exception as e:
            print(f"✗ Failed to connect: {e}")
            return False
    
    def disconnect(self):
        """Close serial connection"""
        if self.ser:
            self.ser.close()
            print("✓ Disconnected")
    
    def send_command(self, command: int):
        """Send a read command to the node"""
        self.ser.write(bytes([command]))
        self.ser.flush()
    
    def send_write_command(self, command: int, data: bytes):
        """Send a write command with data and checksum"""
        message = bytes([command]) + data
        checksum = sum(data) & 0xFF
        message += bytes([checksum])
        self.ser.write(message)
        self.ser.flush()
    
    def read_response(self, expected_bytes: int) -> bytes:
        """Read response from node"""
        data = self.ser.read(expected_bytes)
        if len(data) < expected_bytes:
            raise TimeoutError(f"Expected {expected_bytes} bytes, got {len(data)}")
        return data
    
    def test_read_api_level(self) -> bool:
        """Test: Read API level (READ_ADDRESS command)"""
        print("\n[TEST] Reading API Level...")
        try:
            self.send_command(READ_ADDRESS)
            data = self.read_response(1)
            api_level = data[0]
            
            if api_level == NODE_API_LEVEL:
                print(f"  ✓ API Level: {api_level} (expected {NODE_API_LEVEL})")
                self.test_results.append(("API Level", True, api_level))
                return True
            else:
                print(f"  ✗ API Level: {api_level} (expected {NODE_API_LEVEL})")
                self.test_results.append(("API Level", False, api_level))
                return False
        except Exception as e:
            print(f"  ✗ Error: {e}")
            self.test_results.append(("API Level", False, str(e)))
            return False
    
    def test_read_frequency(self) -> bool:
        """Test: Read current frequency"""
        print("\n[TEST] Reading Frequency...")
        try:
            self.send_command(READ_FREQUENCY)
            data = self.read_response(2)
            freq = struct.unpack('>H', data)[0]  # Big-endian uint16
            
            print(f"  ✓ Frequency: {freq} MHz")
            self.test_results.append(("Read Frequency", True, freq))
            return True
        except Exception as e:
            print(f"  ✗ Error: {e}")
            self.test_results.append(("Read Frequency", False, str(e)))
            return False
    
    def test_write_frequency(self, new_freq: int) -> bool:
        """Test: Write new frequency"""
        print(f"\n[TEST] Writing Frequency ({new_freq} MHz)...")
        try:
            # Send write frequency command
            data = struct.pack('>H', new_freq)  # Big-endian uint16
            self.send_write_command(WRITE_FREQUENCY, data)
            
            time.sleep(0.2)  # Wait for write to complete
            
            # Read back to verify
            self.send_command(READ_FREQUENCY)
            response = self.read_response(2)
            read_freq = struct.unpack('>H', response)[0]
            
            if read_freq == new_freq:
                print(f"  ✓ Frequency set to: {read_freq} MHz")
                self.state.frequency = new_freq
                self.test_results.append(("Write Frequency", True, read_freq))
                return True
            else:
                print(f"  ✗ Frequency mismatch: {read_freq} (expected {new_freq})")
                self.test_results.append(("Write Frequency", False, read_freq))
                return False
        except Exception as e:
            print(f"  ✗ Error: {e}")
            self.test_results.append(("Write Frequency", False, str(e)))
            return False
    
    def test_read_threshold(self) -> bool:
        """Test: Read enter/exit thresholds"""
        print("\n[TEST] Reading Thresholds...")
        try:
            self.send_command(READ_ENTER_AT_LEVEL)
            enter_data = self.read_response(1)
            enter_level = enter_data[0]
            
            self.send_command(READ_EXIT_AT_LEVEL)
            exit_data = self.read_response(1)
            exit_level = exit_data[0]
            
            print(f"  ✓ Enter at: {enter_level}, Exit at: {exit_level}")
            self.test_results.append(("Read Thresholds", True, (enter_level, exit_level)))
            return True
        except Exception as e:
            print(f"  ✗ Error: {e}")
            self.test_results.append(("Read Thresholds", False, str(e)))
            return False
    
    def test_firmware_info(self) -> bool:
        """Test: Read firmware version strings"""
        print("\n[TEST] Reading Firmware Info...")
        try:
            # Read FW version
            self.send_command(READ_FW_VERSION)
            version_data = self.read_response(32)
            version = version_data.split(b'\x00')[0].decode('ascii', errors='ignore')
            
            # Read build date
            self.send_command(READ_FW_BUILDDATE)
            date_data = self.read_response(32)
            build_date = date_data.split(b'\x00')[0].decode('ascii', errors='ignore')
            
            # Read processor type
            self.send_command(READ_FW_PROCTYPE)
            proc_data = self.read_response(16)
            proc_type = proc_data.split(b'\x00')[0].decode('ascii', errors='ignore')
            
            print(f"  ✓ Version: {version}")
            print(f"  ✓ Build Date: {build_date}")
            print(f"  ✓ Processor: {proc_type}")
            self.test_results.append(("Firmware Info", True, version))
            return True
        except Exception as e:
            print(f"  ✗ Error: {e}")
            self.test_results.append(("Firmware Info", False, str(e)))
            return False
    
    def test_rssi_reading(self) -> bool:
        """Test: Read RSSI peak"""
        print("\n[TEST] Reading RSSI...")
        try:
            self.send_command(READ_NODE_RSSI_PEAK)
            data = self.read_response(1)
            rssi = data[0]
            
            print(f"  ✓ RSSI Peak: {rssi}")
            self.test_results.append(("RSSI Reading", True, rssi))
            return True
        except Exception as e:
            print(f"  ✗ Error: {e}")
            self.test_results.append(("RSSI Reading", False, str(e)))
            return False
    
    def run_all_tests(self) -> bool:
        """Run complete test suite"""
        print("=" * 60)
        print("StarForgeOS Node Mode - Protocol Test Suite")
        print("=" * 60)
        
        tests = [
            self.test_read_api_level,
            self.test_firmware_info,
            self.test_read_frequency,
            self.test_write_frequency(5740),  # Test write
            self.test_write_frequency(5800),  # Write back
            self.test_read_threshold,
            self.test_rssi_reading,
        ]
        
        passed = 0
        failed = 0
        
        for test in tests:
            try:
                if callable(test):
                    result = test()
                else:
                    result = test
                if result:
                    passed += 1
                else:
                    failed += 1
            except Exception as e:
                print(f"  ✗ Test crashed: {e}")
                failed += 1
            
            time.sleep(0.1)  # Small delay between tests
        
        # Print summary
        print("\n" + "=" * 60)
        print("Test Summary")
        print("=" * 60)
        for name, success, value in self.test_results:
            status = "✓ PASS" if success else "✗ FAIL"
            print(f"{status:8} {name:25} {value}")
        
        print("=" * 60)
        print(f"Total: {passed + failed} tests, {passed} passed, {failed} failed")
        print("=" * 60)
        
        return failed == 0


def main():
    if len(sys.argv) < 2:
        print("Usage: python mock_rotorhazard.py <serial_port>")
        print("Example: python mock_rotorhazard.py /dev/ttyUSB0")
        sys.exit(1)
    
    port = sys.argv[1]
    server = MockRotorHazardServer(port)
    
    if not server.connect():
        sys.exit(1)
    
    try:
        success = server.run_all_tests()
        sys.exit(0 if success else 1)
    finally:
        server.disconnect()


if __name__ == "__main__":
    main()

