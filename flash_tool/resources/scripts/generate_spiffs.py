#!/usr/bin/env python3
"""
Generate SPIFFS partition image with custom config.json

This script creates a SPIFFS filesystem image containing a custom pin configuration.
The image can be flashed to ESP32 alongside the firmware.
"""

import json
import sys
import os
import subprocess
import tempfile
import shutil
from pathlib import Path

def find_mkspiffs():
    """Find mkspiffs executable (from PlatformIO or system)"""
    
    # Try common PlatformIO locations
    home = Path.home()
    platformio_paths = [
        home / ".platformio" / "packages" / "tool-mkspiffs",
        home / ".platformio" / "packages" / "tool-mkspiffs" / "mkspiffs",
        home / ".platformio" / "packages" / "tool-mkspiffs" / "mkspiffs.exe",
    ]
    
    for pio_path in platformio_paths:
        if pio_path.exists():
            return str(pio_path)
        # Check for platform-specific binaries
        for exe in pio_path.glob("mkspiffs*"):
            if exe.is_file() and os.access(exe, os.X_OK | os.R_OK):
                return str(exe)
    
    # Try system PATH
    if shutil.which("mkspiffs"):
        return "mkspiffs"
    
    # Try bundled mkspiffs (if packaged with flash tool)
    script_dir = Path(__file__).parent
    bundled_paths = [
        script_dir / "bin" / "mkspiffs",
        script_dir / "bin" / "mkspiffs.exe",
        script_dir.parent / "bin" / "mkspiffs",
        script_dir.parent / "bin" / "mkspiffs.exe",
    ]
    
    for bundled in bundled_paths:
        if bundled.exists() and os.access(bundled, os.X_OK | os.R_OK):
            return str(bundled)
    
    raise FileNotFoundError(
        "mkspiffs not found! Please install PlatformIO or provide mkspiffs binary"
    )

def create_config_json(config, output_file):
    """Create config.json file from configuration dict"""
    with open(output_file, 'w') as f:
        json.dump(config, f, indent=2)
    print(f"✓ Created config.json: {output_file}")

def generate_spiffs_image(source_dir, output_image, block_size=4096, page_size=256, image_size=0x170000):
    """
    Generate SPIFFS image from source directory
    
    Args:
        source_dir: Directory containing files to include
        output_image: Output .bin file path
        block_size: SPIFFS block size (default 4096 for ESP32)
        page_size: SPIFFS page size (default 256 for ESP32)
        image_size: Total SPIFFS partition size (default 0x170000 = 1.5MB)
    """
    
    mkspiffs = find_mkspiffs()
    print(f"✓ Found mkspiffs: {mkspiffs}")
    
    # Build mkspiffs command
    cmd = [
        mkspiffs,
        "-c", str(source_dir),      # Source directory
        "-b", str(block_size),       # Block size
        "-p", str(page_size),        # Page size
        "-s", hex(image_size),       # Image size
        str(output_image)            # Output file
    ]
    
    print(f"✓ Running: {' '.join(cmd)}")
    
    try:
        result = subprocess.run(cmd, check=True, capture_output=True, text=True)
        if result.stdout:
            print(result.stdout)
        print(f"✓ SPIFFS image created: {output_image}")
        print(f"  Size: {os.path.getsize(output_image):,} bytes")
        return True
    except subprocess.CalledProcessError as e:
        print(f"✗ mkspiffs failed: {e}")
        if e.stdout:
            print(f"  stdout: {e.stdout}")
        if e.stderr:
            print(f"  stderr: {e.stderr}")
        return False

def generate_spiffs_with_config(config_dict, output_image, include_web_files=False):
    """
    Generate SPIFFS image with custom config.json
    
    Args:
        config_dict: Configuration dictionary to write as config.json
        output_image: Output .bin file path
        include_web_files: If True, also include web UI files (index.html, style.css, app.js)
    """
    
    # Create temporary directory for SPIFFS contents
    with tempfile.TemporaryDirectory() as temp_dir:
        temp_path = Path(temp_dir)
        
        # Create config.json
        config_file = temp_path / "config.json"
        create_config_json(config_dict, config_file)
        
        # Optionally include web files (for firmware that needs them)
        if include_web_files:
            print("Note: Web files not included (use existing SPIFFS or PlatformIO uploadfs)")
        
        # Generate SPIFFS image
        return generate_spiffs_image(temp_dir, output_image)

def main():
    if len(sys.argv) < 3:
        print("Usage: generate_spiffs.py <config.json> <output.bin>")
        print("")
        print("Example config.json:")
        print(json.dumps({
            "custom_pins": {
                "enabled": True,
                "rssi_input": 3,
                "rx5808_data": 6,
                "rx5808_clk": 4,
                "rx5808_sel": 7,
                "mode_switch": 1
            }
        }, indent=2))
        sys.exit(1)
    
    config_json_path = sys.argv[1]
    output_bin_path = sys.argv[2]
    
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
    
    # Generate SPIFFS image
    print(f"\n=== Generating SPIFFS Image ===")
    print(f"Config: {config_json_path}")
    print(f"Output: {output_bin_path}\n")
    
    success = generate_spiffs_with_config(config, output_bin_path)
    
    if success:
        print(f"\n✓ Done! Flash with:")
        print(f"  esptool.py --chip esp32 --port /dev/ttyUSB0 write_flash 0x290000 {output_bin_path}")
        sys.exit(0)
    else:
        print("\n✗ Failed to generate SPIFFS image")
        sys.exit(1)

if __name__ == "__main__":
    main()

