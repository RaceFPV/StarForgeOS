# GitHub Actions Workflows

## Automatic Firmware Builds

This repository uses GitHub Actions to automatically build firmware for all supported platforms.

### What Gets Built

The workflow builds firmware for:
- **ESP32-C3 SuperMini** (tested, recommended)
- **ESP32-C3** (generic dev module)
- **ESP32** (original ESP32 dev module)
- **ESP32-S3** (with PSRAM support)
- **ESP32-S2** (single-core)

### When Builds Run

Builds are triggered on:
- **Push** to `main`, `master`, or `develop` branches
- **Pull requests** to these branches
- **Releases** (automatically uploads binaries to release)
- **Manual trigger** via Actions tab

### GitHub Actions Free Tier

✅ **Free for public repos** - Unlimited build minutes
✅ **2,000 minutes/month for private repos**
✅ Each build takes ~5-10 minutes total (all 5 platforms)

### Downloading Builds

#### From Actions Tab
1. Go to **Actions** tab in GitHub
2. Click on a workflow run
3. Scroll to **Artifacts** section
4. Download `firmware-<platform>.zip`

#### From Releases
When you create a release, binaries are automatically attached:
1. Go to **Releases**
2. Download `StarForge-<platform>.zip`

### What's Included

Each build artifact contains:
- `firmware.bin` - Main application
- `bootloader.bin` - ESP32 bootloader
- `partitions.bin` - Partition table
- `spiffs.bin` - Web interface files
- `FLASH_INSTRUCTIONS.txt` - How to flash

### Manual Flashing

Using esptool.py (included with PlatformIO):
```bash
esptool.py --chip auto --port /dev/ttyUSB0 --baud 921600 write_flash \
  0x0000 bootloader.bin \
  0x8000 partitions.bin \
  0x10000 firmware.bin \
  0x290000 spiffs.bin
```

Or use PlatformIO:
```bash
# Flash firmware
pio run -e esp32-c3-supermini --target upload --upload-port /dev/ttyUSB0

# Flash web interface
pio run -e esp32-c3-supermini --target uploadfs --upload-port /dev/ttyUSB0
```

### Build Cache

The workflow caches PlatformIO dependencies to speed up builds:
- First build: ~8-10 minutes
- Subsequent builds: ~3-5 minutes

### Troubleshooting

**Build fails for one platform:**
- Check the Actions log for details
- The workflow continues building other platforms even if one fails
- Common issue: platform-specific dependencies

**Out of GitHub Actions minutes (private repo):**
- You get 2,000 minutes/month free
- Each full build uses ~10 minutes
- That's 200 builds per month
- Consider making repo public for unlimited minutes

**Want to test before pushing:**
- Build locally: `pio run -e esp32-c3-supermini`
- Run tests: `pio test -e esp32-c3-supermini`

### Advanced Usage

#### Manual Trigger
1. Go to **Actions** tab
2. Select "Build PlatformIO Firmware"
3. Click **Run workflow**
4. Choose branch and click **Run**

#### Build Specific Platform Only
Edit `.github/workflows/build.yml` and comment out unwanted platforms in the matrix.

#### Add New Platform
1. Add environment to `platformio.ini`
2. Add environment name to workflow matrix
3. Push and it will build automatically

### Cost Estimate (Private Repo)

| Activity | Minutes Used | Builds/Month Available |
|----------|--------------|------------------------|
| Push to main | 10 min | 200 |
| Pull request | 10 min | 200 |
| Release | 10 min | 200 |
| **Total free** | **2,000/month** | **~200 full builds** |

For most projects, this is more than enough. If you need more, consider:
- Making the repo public (unlimited minutes)
- Disabling builds for less-used platforms
- Building only on release tags

