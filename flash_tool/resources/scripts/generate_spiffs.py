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
    """Find mkspiffs executable (PlatformIO preferred, then bundled, then system)"""
    
    # FIRST: Try PlatformIO's mkspiffs (same one that works with uploadfs)
    # PlatformIO uses: mkspiffs_espressif32_arduino in ~/.platformio/tools/tool-mkspiffs/
    home = Path.home()
    platformio_tools = home / ".platformio" / "tools" / "tool-mkspiffs"
    platformio_packages = home / ".platformio" / "packages"
    
    # Check PlatformIO tools directory (where mkspiffs_espressif32_arduino lives)
    pio_tool_paths = [
        platformio_tools / "mkspiffs_espressif32_arduino",  # Exact name PlatformIO uses
        platformio_tools / "mkspiffs",
    ]
    
    for pio_tool in pio_tool_paths:
        if pio_tool.exists() and pio_tool.is_file():
            if not os.access(pio_tool, os.X_OK):
                try:
                    os.chmod(pio_tool, 0o755)
                except Exception:
                    pass
            if os.access(pio_tool, os.X_OK | os.R_OK):
                print(f"✓ Using PlatformIO's mkspiffs: {pio_tool}")
                return str(pio_tool)
    
    # Also check packages directory (alternative location)
    tool_mkspiffs_dirs = list(platformio_packages.glob("tool-mkspiffs*"))
    for tool_dir in tool_mkspiffs_dirs:
        if tool_dir.is_dir():
            for exe in tool_dir.rglob("mkspiffs*"):
                if exe.is_file() and not exe.suffix == ".py":
                    if os.access(exe, os.X_OK | os.R_OK):
                        print(f"✓ Using PlatformIO's mkspiffs: {exe}")
                        return str(exe)
    
    # FALLBACK: Try bundled version (for self-contained app)
    import platform
    system = platform.system().lower()
    if system == "darwin" or system == "macos":
        binary_name = "mkspiffs-macos"
    elif system == "windows":
        binary_name = "mkspiffs-win64.exe"
    else:
        binary_name = "mkspiffs-linux"
    
    script_dir = Path(__file__).parent
    dev_bundled_base = script_dir.parent.parent / "resources" / "bin"
    script_bin = script_dir.parent / "bin"
    
    bundled_paths = [
        dev_bundled_base / binary_name,
        dev_bundled_base / "mkspiffs",
        dev_bundled_base / "mkspiffs.exe",
        script_bin / binary_name,
        script_bin / "mkspiffs",
        script_bin / "mkspiffs.exe",
    ]
    
    # Also check if we're in a packaged Electron app (resourcesPath is passed via env)
    resources_path = os.environ.get("ELECTRON_RESOURCES_PATH")
    if resources_path:
        packaged_bin = Path(resources_path) / binary_name
        bundled_paths.insert(0, packaged_bin)
        bundled_paths.insert(1, Path(resources_path) / "mkspiffs")
        bundled_paths.insert(2, Path(resources_path) / "mkspiffs.exe")
    
    for bundled in bundled_paths:
        if bundled.exists() and bundled.is_file():
            # Make executable if needed
            if not os.access(bundled, os.X_OK):
                try:
                    os.chmod(bundled, 0o755)
                    print(f"✓ Made executable: {bundled}")
                except Exception as e:
                    print(f"⚠ Warning: Could not make {bundled} executable: {e}")
            if os.access(bundled, os.X_OK | os.R_OK):
                print(f"✓ Using bundled mkspiffs: {bundled}")
                return str(bundled)
    
    # Fallback: Try PlatformIO locations
    home = Path.home()
    platformio_base = home / ".platformio" / "packages"
    
    # Look for tool-mkspiffs directory
    tool_mkspiffs_dirs = list(platformio_base.glob("tool-mkspiffs*"))
    
    # Also check direct paths
    direct_paths = [
        platformio_base / "tool-mkspiffs" / "mkspiffs",
        platformio_base / "tool-mkspiffs" / "mkspiffs.exe",
        platformio_base / "tool-mkspiffs" / "bin" / "mkspiffs",
        platformio_base / "tool-mkspiffs" / "bin" / "mkspiffs.exe",
    ]
    
    # Search in tool-mkspiffs directories
    for tool_dir in tool_mkspiffs_dirs:
        if tool_dir.is_dir():
            # Look for mkspiffs executable in various subdirectories
            search_paths = [
                tool_dir / "mkspiffs",
                tool_dir / "mkspiffs.exe",
                tool_dir / "bin" / "mkspiffs",
                tool_dir / "bin" / "mkspiffs.exe",
            ]
            # Also search recursively
            for exe in tool_dir.rglob("mkspiffs*"):
                if exe.is_file() and not exe.suffix == ".py":  # Skip Python files
                    search_paths.append(exe)
            
            for exe_path in search_paths:
                if exe_path.exists() and exe_path.is_file():
                    # Try to make it executable if it isn't already
                    if not os.access(exe_path, os.X_OK):
                        try:
                            os.chmod(exe_path, 0o755)  # rwxr-xr-x
                            print(f"✓ Made executable: {exe_path}")
                        except Exception as e:
                            print(f"⚠ Warning: Could not make {exe_path} executable: {e}")
                    if os.access(exe_path, os.X_OK | os.R_OK):
                        return str(exe_path)
    
    # Check direct paths
    for pio_path in direct_paths:
        if pio_path.exists() and pio_path.is_file():
            if not os.access(pio_path, os.X_OK):
                try:
                    os.chmod(pio_path, 0o755)
                    print(f"✓ Made executable: {pio_path}")
                except Exception as e:
                    print(f"⚠ Warning: Could not make {pio_path} executable: {e}")
            if os.access(pio_path, os.X_OK | os.R_OK):
                return str(pio_path)
    
    # Try system PATH
    mkspiffs_path = shutil.which("mkspiffs")
    if mkspiffs_path:
        return mkspiffs_path
    
    # Provide helpful error message with search locations
    searched_locations = [
        str(bundled_base / binary_name),
        str(bundled_base),
        str(platformio_base / "tool-mkspiffs*"),
        str(platformio_base),
        "system PATH",
    ]
    
    error_msg = (
        "mkspiffs not found!\n\n"
        "Searched in:\n"
        + "\n".join(f"  - {loc}" for loc in searched_locations) +
        "\n\n"
        "To fix:\n"
        "1. Install PlatformIO: pip install platformio\n"
        "2. Or download mkspiffs from: https://github.com/igrr/mkspiffs/releases\n"
        "3. Or run: pio platform install espressif32 (this installs mkspiffs)\n"
    )
    
    raise FileNotFoundError(error_msg)

def create_config_json(config, output_file):
    """Create config.json file from configuration dict"""
    with open(output_file, 'w') as f:
        json.dump(config, f, indent=2)
    print(f"✓ Created config.json: {output_file}")

def generate_spiffs_image(source_dir, output_image, block_size=4096, page_size=256, image_size=1441792):
    """
    Generate SPIFFS image from source directory
    Uses exact same parameters as PlatformIO's buildfs target
    
    Args:
        source_dir: Directory containing files to include
        output_image: Output .bin file path
        block_size: SPIFFS block size (4096 for ESP32, matching PlatformIO)
        page_size: SPIFFS page size (256 for ESP32, matching PlatformIO)
        image_size: Total SPIFFS partition size (1441792 bytes = ~1.4MB, matching PlatformIO)
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
    except PermissionError as e:
        print(f"✗ Permission denied running mkspiffs: {e}")
        print(f"  Path: {mkspiffs}")
        print(f"  Try running: chmod +x '{mkspiffs}'")
        return False
    except subprocess.CalledProcessError as e:
        print(f"✗ mkspiffs failed: {e}")
        if e.stdout:
            print(f"  stdout: {e.stdout}")
        if e.stderr:
            print(f"  stderr: {e.stderr}")
        return False
    except Exception as e:
        print(f"✗ Unexpected error running mkspiffs: {e}")
        print(f"  Path: {mkspiffs}")
        return False

def extract_spiffs_files(spiffs_image, output_dir, block_size=4096, page_size=256, image_size=0x170000):
    """
    Extract files from existing SPIFFS image using mkspiffs unpack
    
    Args:
        spiffs_image: Path to existing SPIFFS .bin file
        output_dir: Directory to extract files to
        block_size: SPIFFS block size (default 4096 for ESP32)
        page_size: SPIFFS page size (default 256 for ESP32)
        image_size: Total SPIFFS partition size (default 0x170000 = 1.5MB)
    """
    mkspiffs = find_mkspiffs()
    output_path = Path(output_dir)
    output_path.mkdir(parents=True, exist_ok=True)
    
    # Use mkspiffs to unpack the SPIFFS image
    # Syntax: mkspiffs -u <dest_dir> -b <block_size> -p <page_size> -s <image_size> <image_file>
    cmd = [
        mkspiffs,
        "-u", str(output_path),      # Unpack to directory
        "-b", str(block_size),        # Block size
        "-p", str(page_size),         # Page size
        "-s", hex(image_size),        # Image size
        str(spiffs_image)             # Source SPIFFS image (must come last)
    ]
    
    print(f"Running: {' '.join(cmd)}")
    
    try:
        result = subprocess.run(cmd, check=True, capture_output=True, text=True)
        if result.stdout:
            print(result.stdout)
        if result.stderr:
            print(f"stderr: {result.stderr}")
        
        # Verify files were extracted
        extracted_files = list(output_path.iterdir())
        print(f"✓ Extracted {len(extracted_files)} files from SPIFFS image: {spiffs_image}")
        for f in extracted_files:
            print(f"  - {f.name} ({f.stat().st_size} bytes)")
        return True
    except subprocess.CalledProcessError as e:
        print(f"⚠ Warning: Could not extract SPIFFS files: {e}")
        if e.stdout:
            print(f"  stdout: {e.stdout}")
        if e.stderr:
            print(f"  stderr: {e.stderr}")
        return False

def generate_spiffs_with_config(config_dict, output_image, firmware_path=None):
    """
    Generate SPIFFS image with custom config.json
    Uses the same approach as PlatformIO: copy files from data/ directory and add config.json
    
    Args:
        config_dict: Configuration dictionary to write as config.json
        output_image: Output .bin file path
        firmware_path: Path to firmware directory (to find data/ folder)
    """
    
    # Create temporary directory for SPIFFS contents
    with tempfile.TemporaryDirectory() as temp_dir:
        temp_path = Path(temp_dir)
        
        # Find data directory (same as PlatformIO uses)
        data_dir = None
        if firmware_path:
            firmware_path_obj = Path(firmware_path)
            # Try common locations relative to firmware path
            possible_data_dirs = [
                firmware_path_obj / "data",  # Direct: firmware_path/data/
                firmware_path_obj.parent / "data",  # One level up
                firmware_path_obj.parent.parent / "data",  # Two levels up (for .pio/build/<env>/)
            ]
            
            for possible_dir in possible_data_dirs:
                if possible_dir.exists() and possible_dir.is_dir():
                    data_dir = possible_dir
                    break
        
        # If not found, try relative to script location (for development)
        if not data_dir:
            script_data_dir = Path(__file__).parent.parent.parent.parent / "data"
            if script_data_dir.exists():
                data_dir = script_data_dir
        
        # Copy web files from data directory (same as PlatformIO uploadfs)
        if data_dir and data_dir.exists():
            print(f"✓ Using data directory: {data_dir}")
            web_files = ["index.html", "style.css", "app.js"]
            for web_file in web_files:
                src_file = data_dir / web_file
                if src_file.exists():
                    import shutil
                    shutil.copy2(src_file, temp_path / web_file)
                    print(f"✓ Copied {web_file}")
                else:
                    print(f"⚠ Warning: {web_file} not found in data directory")
        else:
            print(f"⚠ Warning: data directory not found, web files may be missing")
            print(f"  Searched in: {firmware_path} and parent directories")
        
        # Add config.json (this is what we're customizing)
        config_file = temp_path / "config.json"
        create_config_json(config_dict, config_file)
        
        # List what we're including
        files_included = list(temp_path.iterdir())
        print(f"✓ Files to include in SPIFFS: {[f.name for f in files_included]}")
        
        # Generate SPIFFS image (same way PlatformIO does it)
        return generate_spiffs_image(temp_dir, output_image)

def main():
    if len(sys.argv) < 3:
        print("Usage: generate_spiffs.py <config.json> <output.bin> [firmware_path]")
        print("")
        print("Arguments:")
        print("  config.json   - Configuration file to include")
        print("  output.bin    - Output SPIFFS image path")
        print("  firmware_path - Optional: Path to firmware directory (to find data/ folder)")
        print("")
        print("Example config.json:")
        print(json.dumps({
            "default_mode": "standalone",
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
    firmware_path = sys.argv[3] if len(sys.argv) > 3 else None
    
    # Load configuration
    try:
        with open(config_json_path, 'r') as f:
            config = json.load(f)
    except Exception as e:
        print(f"✗ Failed to read config: {e}")
        sys.exit(1)
    
    # Validate config - must have at least default_mode or custom_pins
    if "default_mode" not in config and "custom_pins" not in config:
        print("✗ Config must contain at least 'default_mode' or 'custom_pins'")
        sys.exit(1)
    
    # Ensure default_mode is present (required for mode selection)
    if "default_mode" not in config:
        print("⚠ Warning: 'default_mode' not found, defaulting to 'standalone'")
        config["default_mode"] = "standalone"
    
    # Generate SPIFFS image
    print(f"\n=== Generating SPIFFS Image ===")
    print(f"Config: {config_json_path}")
    print(f"Output: {output_bin_path}")
    if firmware_path:
        print(f"Firmware path: {firmware_path} (will look for data/ directory)")
    print()
    
    success = generate_spiffs_with_config(config, output_bin_path, firmware_path=firmware_path)
    
    if success:
        print(f"\n✓ Done! Flash with:")
        print(f"  esptool.py --chip esp32 --port /dev/ttyUSB0 write_flash 0x290000 {output_bin_path}")
        sys.exit(0)
    else:
        print("\n✗ Failed to generate SPIFFS image")
        sys.exit(1)

if __name__ == "__main__":
    main()

