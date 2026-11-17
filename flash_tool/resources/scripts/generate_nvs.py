#!/usr/bin/env python3
"""
Generate NVS partition image with custom pin configuration

This script creates an NVS partition binary containing custom pin settings.
The NVS partition can be flashed to ESP32 using esptool.

Uses ESP-IDF's nvs_partition_gen.py or creates NVS binary manually.
"""

import json
import sys
import os
import subprocess
import tempfile
import struct
from pathlib import Path

def find_nvs_partition_gen():
    """Find ESP-IDF's nvs_partition_gen.py tool"""
    
    # Try common ESP-IDF locations
    home = Path.home()
    idf_paths = [
        home / ".espressif" / "esp-idf" / "components" / "nvs_flash" / "nvs_partition_generator" / "nvs_partition_gen.py",
        Path(os.environ.get('IDF_PATH', '')) / "components" / "nvs_flash" / "nvs_partition_generator" / "nvs_partition_gen.py",
    ]
    
    for idf_path in idf_paths:
        if idf_path.exists():
            return str(idf_path)
    
    # Try system PATH
    if os.path.exists("/usr/bin/nvs_partition_gen.py"):
        return "/usr/bin/nvs_partition_gen.py"
    
    return None

def create_nvs_csv(config_dict, output_csv):
    """Create NVS CSV file from configuration dictionary"""
    
    # NVS CSV format: key,type,encoding,value
    # Types: u8, i8, u16, i16, u32, i32, u64, i64, string, blob, blob_hex
    # Encoding: (for strings) - size
    
    lines = [
        "key,type,encoding,value"
    ]
    
    custom_pins = config_dict.get("custom_pins", {})
    
    # Save enabled flag
    enabled = custom_pins.get("enabled", False)
    lines.append(f"pin_enabled,u8,,{1 if enabled else 0}")
    
    if enabled:
        # Core pins (required)
        if "rssi_input" in custom_pins:
            lines.append(f"pin_rssi_input,u8,,{custom_pins['rssi_input']}")
        if "rx5808_data" in custom_pins:
            lines.append(f"pin_rx5808_data,u8,,{custom_pins['rx5808_data']}")
        if "rx5808_clk" in custom_pins:
            lines.append(f"pin_rx5808_clk,u8,,{custom_pins['rx5808_clk']}")
        if "rx5808_sel" in custom_pins:
            lines.append(f"pin_rx5808_sel,u8,,{custom_pins['rx5808_sel']}")
        
        # Optional pins
        if "power_button" in custom_pins and custom_pins["power_button"] > 0:
            lines.append(f"pin_power_button,u8,,{custom_pins['power_button']}")
        if "battery_adc" in custom_pins and custom_pins["battery_adc"] > 0:
            lines.append(f"pin_battery_adc,u8,,{custom_pins['battery_adc']}")
        if "audio_dac" in custom_pins and custom_pins["audio_dac"] > 0:
            lines.append(f"pin_audio_dac,u8,,{custom_pins['audio_dac']}")
        if "usb_detect" in custom_pins and custom_pins["usb_detect"] > 0:
            lines.append(f"pin_usb_detect,u8,,{custom_pins['usb_detect']}")
        
        # LCD/Touch pins (can be negative, use i8)
        if "lcd_i2c_sda" in custom_pins and custom_pins["lcd_i2c_sda"] >= 0:
            lines.append(f"pin_lcd_i2c_sda,i8,,{custom_pins['lcd_i2c_sda']}")
        if "lcd_i2c_scl" in custom_pins and custom_pins["lcd_i2c_scl"] >= 0:
            lines.append(f"pin_lcd_i2c_scl,i8,,{custom_pins['lcd_i2c_scl']}")
        if "lcd_backlight" in custom_pins and custom_pins["lcd_backlight"] >= 0:
            lines.append(f"pin_lcd_backlight,i8,,{custom_pins['lcd_backlight']}")
    
    with open(output_csv, 'w') as f:
        f.write('\n'.join(lines) + '\n')
    
    print(f"✓ Created NVS CSV: {output_csv}")
    return True

def generate_nvs_with_tool(csv_path, output_bin, size=0x5000):
    """Generate NVS binary using ESP-IDF's nvs_partition_gen.py"""
    
    nvs_tool = find_nvs_partition_gen()
    if not nvs_tool:
        raise FileNotFoundError(
            "ESP-IDF nvs_partition_gen.py not found!\n"
            "Install ESP-IDF or use manual NVS generation"
        )
    
    print(f"✓ Found nvs_partition_gen.py: {nvs_tool}")
    
    # nvs_partition_gen.py input.csv output.bin 0x5000
    cmd = [
        sys.executable,  # Use same Python interpreter
        nvs_tool,
        csv_path,
        output_bin,
        hex(size)
    ]
    
    print(f"✓ Running: {' '.join(cmd)}")
    
    try:
        result = subprocess.run(cmd, check=True, capture_output=True, text=True)
        if result.stdout:
            print(result.stdout)
        if result.stderr:
            print(result.stderr)
        print(f"✓ NVS partition created: {output_bin}")
        print(f"  Size: {os.path.getsize(output_bin):,} bytes")
        return True
    except subprocess.CalledProcessError as e:
        print(f"✗ nvs_partition_gen.py failed: {e}")
        if e.stdout:
            print(f"  stdout: {e.stdout}")
        if e.stderr:
            print(f"  stderr: {e.stderr}")
        return False

def generate_nvs_manual(csv_path, output_bin, size=0x5000):
    """
    Manual NVS binary generation (simplified version)
    
    This is a minimal implementation. For production, use ESP-IDF's tool.
    """
    print("⚠ Using manual NVS generation (limited functionality)")
    print("⚠ For best results, install ESP-IDF and use nvs_partition_gen.py")
    
    # Read CSV
    entries = []
    with open(csv_path, 'r') as f:
        lines = f.readlines()
        for line in lines[1:]:  # Skip header
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            parts = [p.strip() for p in line.split(',')]
            if len(parts) >= 4:
                entries.append({
                    'key': parts[0],
                    'type': parts[1],
                    'encoding': parts[2],
                    'value': parts[3]
                })
    
    # Create minimal NVS binary structure
    # Note: This is a simplified version. Full NVS format is complex.
    # For production use, prefer ESP-IDF's nvs_partition_gen.py
    
    print(f"⚠ Manual NVS generation not fully implemented")
    print(f"⚠ Please install ESP-IDF for proper NVS generation")
    return False

def generate_nvs_with_config(config_dict, output_bin, nvs_size=0x5000):
    """
    Generate NVS partition binary with custom pin configuration
    
    Args:
        config_dict: Configuration dictionary
        output_bin: Output .bin file path
        nvs_size: NVS partition size (default 0x5000 = 20KB)
    """
    
    with tempfile.TemporaryDirectory() as temp_dir:
        temp_path = Path(temp_dir)
        csv_path = temp_path / "nvs_data.csv"
        
        # Create CSV from config
        create_nvs_csv(config_dict, csv_path)
        
        # Try ESP-IDF tool first
        try:
            if generate_nvs_with_tool(csv_path, output_bin, nvs_size):
                return True
        except FileNotFoundError:
            print("ESP-IDF tool not found, trying manual generation...")
        
        # Fallback to manual (may not work fully)
        return generate_nvs_manual(csv_path, output_bin, nvs_size)

def main():
    if len(sys.argv) < 3:
        print("Usage: generate_nvs.py <config.json> <output.bin> [nvs_size]")
        print("")
        print("Example config.json:")
        print(json.dumps({
            "custom_pins": {
                "enabled": True,
                "rssi_input": 3,
                "rx5808_data": 6,
                "rx5808_clk": 4,
                "rx5808_sel": 7
            }
        }, indent=2))
        print("")
        print("Note: Requires ESP-IDF's nvs_partition_gen.py")
        print("      Install ESP-IDF or provide path via IDF_PATH environment variable")
        sys.exit(1)
    
    config_json_path = sys.argv[1]
    output_bin_path = sys.argv[2]
    nvs_size = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0x5000
    
    # Load configuration
    try:
        with open(config_json_path, 'r') as f:
            config = json.load(f)
    except Exception as e:
        print(f"✗ Failed to read config: {e}")
        sys.exit(1)
    
    # Validate config
    if "custom_pins" not in config:
        print("✗ Missing 'custom_pins' section in config")
        sys.exit(1)
    
    # Generate NVS partition
    print(f"\n=== Generating NVS Partition ===")
    print(f"Config: {config_json_path}")
    print(f"Output: {output_bin_path}")
    print(f"Size: {hex(nvs_size)} ({nvs_size} bytes)\n")
    
    success = generate_nvs_with_config(config, output_bin_path, nvs_size)
    
    if success:
        print(f"\n✓ Done! Flash with:")
        print(f"  esptool.py --chip esp32 --port /dev/ttyUSB0 write_flash 0x9000 {output_bin_path}")
        sys.exit(0)
    else:
        print("\n✗ Failed to generate NVS partition")
        print("\nTo install ESP-IDF tools:")
        print("  1. Install ESP-IDF: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/")
        print("  2. Source export script: . $HOME/esp/esp-idf/export.sh")
        print("  3. Or set IDF_PATH environment variable")
        sys.exit(1)

if __name__ == "__main__":
    main()

