const { app, BrowserWindow, ipcMain, dialog } = require('electron');
const path = require('path');
const { spawn } = require('child_process');
const fs = require('fs').promises;
const { SerialPort } = require('serialport');
const axios = require('axios');
const extractZip = require('extract-zip');

let mainWindow;

// Serial monitor state
let monitorPort = null;
let monitorPortPath = null;

// Board configurations matching platformio.ini
const BOARD_CONFIGS = {
  'esp32-c3-supermini': {
    name: 'ESP32-C3 SuperMini',
    chip: 'esp32c3',
    flashAddresses: {
      bootloader: '0x0',
      partitions: '0x8000',
      nvs: '0x9000',
      firmware: '0x10000',
      spiffs: '0x290000'
    }
  },
  'nuclearcounter': {
    name: 'NuclearCounter (ESP32-C3)',
    chip: 'esp32c3',
    flashAddresses: {
      bootloader: '0x0',
      partitions: '0x8000',
      nvs: '0x9000',
      firmware: '0x10000',
      spiffs: '0x290000'
    }
  },
  'esp32-c3': {
    name: 'ESP32-C3 Dev Module',
    chip: 'esp32c3',
    flashAddresses: {
      bootloader: '0x0',
      partitions: '0x8000',
      nvs: '0x9000',
      firmware: '0x10000',
      spiffs: '0x290000'
    }
  },
  'esp32-c6': {
    name: 'ESP32-C6 Dev Module (WiFi 6)',
    chip: 'esp32c6',
    flashAddresses: {
      bootloader: '0x0',
      partitions: '0x8000',
      nvs: '0x9000',
      firmware: '0x10000',
      spiffs: '0x290000'
    }
  },
  'esp32dev': {
    name: 'ESP32 Dev Module (4MB)',
    chip: 'esp32',
    flashAddresses: {
      bootloader: '0x1000',
      partitions: '0x8000',
      nvs: '0x9000',
      firmware: '0x10000',
      spiffs: '0x290000'
    }
  },
  'esp32dev-2mb': {
    name: 'ESP32 Dev Module (2MB)',
    chip: 'esp32',
    flashAddresses: {
      bootloader: '0x1000',
      partitions: '0x8000',
      nvs: '0x9000',
      firmware: '0x10000',
      spiffs: '0x1B0000'  // 2MB partition table: SPIFFS starts at 0x1B0000
    }
  },
  'esp32-s3': {
    name: 'ESP32-S3 Dev Module',
    chip: 'esp32s3',
    flashAddresses: {
      bootloader: '0x0',
      partitions: '0x8000',
      nvs: '0x9000',
      firmware: '0x10000',
      spiffs: '0x190000'  // Default ESP32-S3 partition table uses 0x190000 for SPIFFS
    }
  },
  'esp32-s3-touch': {
    name: 'ESP32-S3 Touch LCD (no PSRAM)',
    chip: 'esp32s3',
    flashAddresses: {
      bootloader: '0x0',
      partitions: '0x8000',
      nvs: '0x9000',
      firmware: '0x10000',
      spiffs: '0x190000'  // Default ESP32-S3 partition table uses 0x190000 for SPIFFS
    }
  },
  'esp32-s3-t-energy': {
    name: 'LilyGo T-Energy (ESP32-S3 with Battery)',
    chip: 'esp32s3',
    flashAddresses: {
      bootloader: '0x0',
      partitions: '0x8000',
      nvs: '0x9000',
      firmware: '0x10000',
      spiffs: '0x190000'  // Default ESP32-S3 partition table uses 0x190000 for SPIFFS
    }
  },
  'esp32-s2': {
    name: 'ESP32-S2 Dev Module',
    chip: 'esp32s2',
    flashAddresses: {
      bootloader: '0x1000',
      partitions: '0x8000',
      nvs: '0x9000',
      firmware: '0x10000',
      spiffs: '0x290000'
    }
  },
  'jc2432w328c': {
    name: 'JC2432W328C (ESP32 with LCD)',
    chip: 'esp32',
    flashAddresses: {
      bootloader: '0x1000',
      partitions: '0x8000',
      nvs: '0x9000',
      firmware: '0x10000',
      spiffs: '0x290000'
    }
  }
};

function createWindow() {
  mainWindow = new BrowserWindow({
    width: 800,
    height: 600,
    webPreferences: {
      nodeIntegration: false,
      contextIsolation: true,
      preload: path.join(__dirname, 'preload.js')
    },
    title: 'StarForge Flash Tool',
    resizable: true,
    autoHideMenuBar: true
  });

  mainWindow.loadFile('index.html');

  // Open DevTools in development
  if (process.env.NODE_ENV === 'development') {
    mainWindow.webContents.openDevTools();
  }
}

app.whenReady().then(() => {
  createWindow();

  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) {
      createWindow();
    }
  });
});

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') {
    app.quit();
  }
});

// IPC Handlers

// Get available serial ports
ipcMain.handle('get-serial-ports', async () => {
  try {
    const ports = await SerialPort.list();
    return ports.map(port => ({
      path: port.path,
      manufacturer: port.manufacturer || 'Unknown',
      serialNumber: port.serialNumber || '',
      productId: port.productId || '',
      vendorId: port.vendorId || '',
      // Check if this looks like an ESP32 device
      isESP32: isLikelyESP32Port(port)
    }));
  } catch (error) {
    console.error('Error listing serial ports:', error);
    return [];
  }
});

// Start serial monitor (read-only)
ipcMain.handle('start-serial-monitor', async (event, options) => {
  const { port, baudRate = 115200 } = options || {};

  if (!port) {
    throw new Error('No serial port specified for monitor');
  }

  // If we're already monitoring this port, do nothing
  if (monitorPort && monitorPortPath === port && monitorPort.readable) {
    return { success: true };
  }

  // Close any existing monitor port first
  if (monitorPort) {
    try {
      monitorPort.close();
    } catch (e) {
      // Ignore errors on close
    }
    monitorPort = null;
    monitorPortPath = null;
  }

  return new Promise((resolve, reject) => {
    const sp = new SerialPort({
      path: port,
      baudRate,
      autoOpen: true
    });

    sp.on('open', () => {
      monitorPort = sp;
      monitorPortPath = port;
      if (mainWindow && !mainWindow.isDestroyed()) {
        mainWindow.webContents.send('serial-monitor-status', {
          type: 'open',
          port,
          baudRate
        });
      }
      resolve({ success: true });
    });

    sp.on('data', (data) => {
      if (mainWindow && !mainWindow.isDestroyed()) {
        mainWindow.webContents.send('serial-monitor-data', data.toString());
      }
    });

    sp.on('error', (err) => {
      if (mainWindow && !mainWindow.isDestroyed()) {
        mainWindow.webContents.send('serial-monitor-status', {
          type: 'error',
          port,
          message: err.message
        });
      }
      // Only reject if this happened before open
      if (!monitorPort || monitorPort !== sp) {
        reject(new Error(`Serial monitor error: ${err.message}`));
      }
    });

    sp.on('close', () => {
      if (mainWindow && !mainWindow.isDestroyed()) {
        mainWindow.webContents.send('serial-monitor-status', {
          type: 'close',
          port
        });
      }
      if (monitorPort === sp) {
        monitorPort = null;
        monitorPortPath = null;
      }
    });
  });
});

// Stop serial monitor
ipcMain.handle('stop-serial-monitor', async () => {
  if (!monitorPort) {
    return { success: true };
  }

  return new Promise((resolve) => {
    try {
      const port = monitorPortPath;
      monitorPort.close((/*err*/) => {
        if (mainWindow && !mainWindow.isDestroyed()) {
          mainWindow.webContents.send('serial-monitor-status', {
            type: 'close',
            port
          });
        }
        monitorPort = null;
        monitorPortPath = null;
        resolve({ success: true });
      });
    } catch (e) {
      monitorPort = null;
      monitorPortPath = null;
      resolve({ success: true });
    }
  });
});

// Helper to identify likely ESP32 ports
function isLikelyESP32Port(port) {
  // Common USB vendor IDs for ESP32 devices
  const esp32VendorIds = [
    '10c4', // Silicon Labs (CP210x) - most common
    '1a86', // QinHeng Electronics (CH340)
    '0403', // FTDI
    '067b', // Prolific
    '303a', // Espressif (native USB on ESP32-S2/S3/C3)
  ];
  
  // Common manufacturer names
  const esp32Manufacturers = [
    'Silicon Labs',
    'Silicon Laboratories',
    'Espressif', // Native USB
    'Expressif', // Sometimes misspelled
    'QinHeng',
    'FTDI',
    'Prolific',
  ];
  
  const vendorId = (port.vendorId || '').toLowerCase();
  const manufacturer = (port.manufacturer || '').toLowerCase();
  
  return esp32VendorIds.includes(vendorId) || 
         esp32Manufacturers.some(m => manufacturer.includes(m.toLowerCase()));
}

// Detect board type by querying the chip
ipcMain.handle('detect-board', async (event, port) => {
  return new Promise((resolve, reject) => {
    let esptoolCmd;
    try {
      esptoolCmd = findEsptool();
    } catch (error) {
      reject(error);
      return;
    }
    
    // Run esptool flash_id to detect chip type
    const args = ['--port', port, 'flash_id'];
    const esptool = spawn(esptoolCmd, args);
    
    let output = '';
    
    esptool.stdout.on('data', (data) => {
      output += data.toString();
    });
    
    esptool.stderr.on('data', (data) => {
      output += data.toString();
    });
    
    esptool.on('close', (code) => {
      if (code === 0 || output.includes('Detecting chip type')) {
        // Parse chip type from output
        const chipType = parseChipType(output);
        const suggestedBoard = suggestBoardFromChip(chipType, output);
        resolve({ 
          success: true, 
          chipType,
          suggestedBoard: suggestedBoard,
          output,
          requiresManualSelection: (chipType.toLowerCase().includes('esp32') && !chipType.toLowerCase().includes('esp32-c') && !chipType.toLowerCase().includes('esp32-s') && suggestedBoard === null)
        });
      } else {
        reject(new Error('Failed to detect chip'));
      }
    });
    
    esptool.on('error', (err) => {
      reject(new Error(`Failed to run esptool: ${err.message}`));
    });
  });
});

// Parse chip type from esptool output
function parseChipType(output) {
  const chipPatterns = [
    /Detecting chip type\.\.\. (ESP32[^\s\n]*)/i,
    /Chip is (ESP32[^\s\n]*)/i,
    /Detecting chip type\.\.\. (.+)/i,
  ];
  
  for (const pattern of chipPatterns) {
    const match = output.match(pattern);
    if (match) {
      return match[1].trim();
    }
  }
  
  return 'Unknown';
}

// Suggest board configuration based on detected chip
// Note: For ESP32, we cannot auto-detect flash size, so we return null to force manual selection
function suggestBoardFromChip(chipType, esptoolOutput = '') {
  const chipLower = chipType.toLowerCase();
  
  if (chipLower.includes('esp32-c3')) {
    return 'esp32-c3-supermini'; // Most common C3 board (NuclearCounter is C3-based as well)
  } else if (chipLower.includes('esp32-s3')) {
    return 'esp32-s3';
  } else if (chipLower.includes('esp32-s2')) {
    return 'esp32-s2';
  } else if (chipLower.includes('esp32')) {
    // ESP32 boards: Cannot auto-detect flash size (2MB vs 4MB)
    // Try to detect from esptool output if available
    const flashSizeMatch = esptoolOutput.match(/Flash.*?size.*?(\d+)\s*MB/i) || 
                          esptoolOutput.match(/Detected.*?(\d+)\s*MB/i) ||
                          esptoolOutput.match(/Flash\s+ID:\s+0x[0-9a-f]+\s+.*?(\d+)\s*MB/i);
    
    if (flashSizeMatch) {
      const flashSize = parseInt(flashSizeMatch[1]);
      if (flashSize === 2) {
        return 'esp32dev-2mb';
      } else if (flashSize === 4) {
        return 'esp32dev';
      }
    }
    
    // Cannot determine flash size - return null to force manual selection
    return null;
  }
  
  return null;
}

// Get board configurations
ipcMain.handle('get-board-configs', async () => {
  return BOARD_CONFIGS;
});

// Fetch releases from GitHub
ipcMain.handle('fetch-github-releases', async () => {
  try {
    // Fetch all releases (including pre-releases) and the latest stable release
    const [allReleasesResponse, latestResponse] = await Promise.all([
      axios.get(
        'https://api.github.com/repos/RaceFPV/StarForgeOS/releases',
        {
          headers: {
            'Accept': 'application/vnd.github.v3+json',
            'User-Agent': 'StarForge-Flasher'
          }
        }
      ),
      axios.get(
        'https://api.github.com/repos/RaceFPV/StarForgeOS/releases/latest',
        {
          headers: {
            'Accept': 'application/vnd.github.v3+json',
            'User-Agent': 'StarForge-Flasher'
          }
        }
      ).catch(() => ({ data: null })) // If latest fails, continue without it
    ]);
    
    if (!allReleasesResponse.data || allReleasesResponse.data.length === 0) {
      throw new Error('No releases found');
    }
    
    // Get the latest stable release tag (excludes pre-releases)
    const latestTag = latestResponse.data ? latestResponse.data.tag_name : null;
    
    // Return all releases with latest indicator
    return {
      releases: allReleasesResponse.data.map(release => ({
        tag: release.tag_name,
        name: release.name || release.tag_name,
        publishedAt: release.published_at,
        isPrerelease: release.prerelease,
        assets: release.assets.map(asset => ({
          name: asset.name,
          url: asset.browser_download_url,
          size: asset.size
        }))
      })),
      latestTag: latestTag // The actual latest stable release tag
    };
  } catch (error) {
    console.error('Error fetching releases:', error);
    if (error.response) {
      throw new Error(`GitHub API error: ${error.response.status} - ${error.response.statusText}`);
    }
    throw new Error('Failed to fetch releases from GitHub. Make sure releases are published.');
  }
});

// Download firmware
ipcMain.handle('download-firmware', async (event, url, boardType) => {
  try {
    const cacheDir = path.join(app.getPath('userData'), 'firmware-cache');
    await fs.mkdir(cacheDir, { recursive: true });
    
    // Include URL hash in cache key to ensure different releases get different cache entries
    // This prevents stale firmware from being used when a new release is downloaded
    const crypto = require('crypto');
    const urlHash = crypto.createHash('md5').update(url).digest('hex').substring(0, 8);
    const zipPath = path.join(cacheDir, `${boardType}-${urlHash}.zip`);
    const extractPath = path.join(cacheDir, `${boardType}-${urlHash}`);
    
    // Clear extract directory if it exists to ensure fresh extraction
    // This prevents stale files from previous extractions
    try {
      const stats = await fs.stat(extractPath).catch(() => null);
      if (stats && stats.isDirectory()) {
        // Remove directory (fs.rm is available in Node 14.14.0+, which Electron uses)
        await fs.rm(extractPath, { recursive: true, force: true });
        event.sender.send('flash-progress', `Cleared old cached firmware...\n`);
      }
    } catch (err) {
      // Ignore errors - directory might not exist or be locked
      // Continue with download anyway
    }
    
    // Download
    event.sender.send('flash-progress', `Downloading firmware from: ${url}\n`);
    const response = await axios({
      method: 'get',
      url: url,
      responseType: 'stream',
      onDownloadProgress: (progressEvent) => {
        const percentCompleted = Math.round((progressEvent.loaded * 100) / progressEvent.total);
        event.sender.send('download-progress', percentCompleted);
      }
    });
    
    const writer = require('fs').createWriteStream(zipPath);
    response.data.pipe(writer);
    
    await new Promise((resolve, reject) => {
      writer.on('finish', resolve);
      writer.on('error', reject);
    });
    
    // Extract to fresh directory
    event.sender.send('flash-progress', `Extracting firmware...\n`);
    await extractZip(zipPath, { dir: extractPath });
    event.sender.send('flash-progress', `✓ Firmware extracted to: ${extractPath}\n`);
    
    return {
      path: extractPath,
      files: await fs.readdir(extractPath)
    };
  } catch (error) {
    console.error('Error downloading firmware:', error);
    throw error;
  }
});

// Find esptool command
function findEsptool() {
  // Determine platform-specific binary name
  let binaryName;
  if (process.platform === 'darwin') {
    binaryName = 'esptool-macos';
  } else if (process.platform === 'win32') {
    binaryName = 'esptool-win64.exe';
  } else {
    binaryName = 'esptool-linux';
  }
  
  // Try bundled version first (production)
  if (app.isPackaged) {
    const bundledPath = path.join(process.resourcesPath, 'resources', 'bin', binaryName);
    if (require('fs').existsSync(bundledPath)) {
      return bundledPath;
    }
  }
  
  // Try development version (running from source)
  const devPath = path.join(__dirname, 'resources', 'bin', binaryName);
  if (require('fs').existsSync(devPath)) {
    return devPath;
  }
  
  // Fallback: try system esptool (for development without downloaded binaries)
  const commands = process.platform === 'win32' 
    ? ['esptool.exe', 'esptool.py', 'esptool']
    : ['esptool.py', 'esptool'];
  
  for (const cmd of commands) {
    try {
      require('child_process').execSync(`${cmd} version`, { stdio: 'ignore' });
      return cmd;
    } catch (e) {
      // Command not found, try next
    }
  }
  
  throw new Error(`esptool not found. Binary should be at: ${devPath}\n\nRun: ./download-esptool.sh`);
}

// Parse partition table to find SPIFFS address
function findSPIFFSInPartitionTable(partitionData, event = null) {
  // ESP-IDF partition table binary format:
  // - MD5 checksum at start (16 bytes) - this is OPTIONAL and may not be present
  // - Partition entries are 32 bytes each:
  //   - 2 bytes: magic (0xAA50, stored as little-endian = 0x50AA)
  //   - 1 byte: type - 0x00=app, 0x01=data
  //   - 1 byte: subtype - 0x00=app, 0x82=spiffs
  //   - 4 bytes: offset (little-endian)
  //   - 4 bytes: size (little-endian)
  //   - 4 bytes: flags (little-endian)
  //   - 16 bytes: name (null-terminated, padded with 0xFF)
  
  // Debug: Always show first 256 bytes as hex for troubleshooting
  if (partitionData.length > 0) {
    const preview = partitionData.slice(0, Math.min(256, partitionData.length));
    const hexPreview = Array.from(preview).map(b => b.toString(16).padStart(2, '0')).join(' ');
    const hexLines = [];
    for (let i = 0; i < hexPreview.length; i += 48) {
      hexLines.push(hexPreview.substring(i, i + 48));
    }
    if (event) {
      event.sender.send('flash-progress', `\nPartition table hex dump (first ${preview.length} bytes):\n`);
      hexLines.forEach(line => {
        event.sender.send('flash-progress', `  ${line}\n`);
      });
    }
    
    // Also try a simple string search for "spiffs" (case-insensitive)
    // ESP-IDF partition table format: 2-byte magic (0xAA50), then type, subtype, offset, size, flags, name
    const spiffsLower = Buffer.from('spiffs', 'utf8');
    const spiffsUpper = Buffer.from('SPIFFS', 'utf8');
    for (let i = 0; i < partitionData.length - 16; i++) {
      const slice = partitionData.slice(i, i + 6);
      if (slice.equals(spiffsLower) || slice.equals(spiffsUpper)) {
        // Found "spiffs" at offset i
        // The name is at bytes 16-31 of a 32-byte entry
        // So the entry starts at i - 16
        const entryStart = i - 16;
        if (entryStart >= 0 && entryStart + 32 <= partitionData.length) {
          // Check for magic bytes at start of entry
          const magic = partitionData.readUInt16LE(entryStart);
          if (magic === 0x50AA) { // Little-endian 0xAA50
            // ESP-IDF format: magic(2) + type(1) + subtype(1) + offset(4) + size(4) + flags(4) + name(16)
            const entryOffset = partitionData.readUInt32LE(entryStart + 4);
            const entrySize = partitionData.readUInt32LE(entryStart + 8);
            const subtype = partitionData.readUInt8(entryStart + 3);
            
            // Validate this looks like a valid SPIFFS partition
            // Subtype 0x82 = SPIFFS
            if (subtype === 0x82 && entryOffset > 0 && entryOffset < 0x10000000 && entrySize > 0 && entrySize < 0x8000000 && (entryOffset & 0xFFF) === 0) {
              if (event) {
                event.sender.send('flash-progress', `✓ Found "spiffs" string at offset 0x${i.toString(16)}, extracted address: 0x${entryOffset.toString(16)}\n`);
              }
              return `0x${entryOffset.toString(16)}`;
            }
          }
        }
      }
    }
  }
  
  // Try to find valid partition entries by scanning the entire buffer
  // ESP-IDF partition table format: magic(2) + type(1) + subtype(1) + offset(4) + size(4) + flags(4) + name(16) = 32 bytes
  const foundPartitions = [];
  const maxEntries = 25; // Reasonable max for partition table
  
  // Try different starting offsets to account for MD5 checksum or padding
  // ESP-IDF partition tables typically start at offset 0, but may have MD5 (16 bytes) or padding
  for (let tableStart = 0; tableStart < Math.min(256, partitionData.length - 32); tableStart += 16) {
    let offset = tableStart;
    const candidates = [];
    
    // Scan for partition entries starting at this offset
    while (offset + 32 <= partitionData.length && candidates.length < maxEntries) {
      // Check for ESP-IDF partition table magic bytes (0xAA50, stored as little-endian = 0x50AA)
      const magic = partitionData.readUInt16LE(offset);
      if (magic !== 0x50AA) {
        // Not a valid partition entry, try next position
        offset += 32;
        continue;
      }
      
      // ESP-IDF format: magic(2) + type(1) + subtype(1) + offset(4) + size(4) + flags(4) + name(16)
      const type = partitionData.readUInt8(offset + 2);
      const subtype = partitionData.readUInt8(offset + 3);
      const entryOffset = partitionData.readUInt32LE(offset + 4);
      const entrySize = partitionData.readUInt32LE(offset + 8);
      const flags = partitionData.readUInt32LE(offset + 12);
      const nameBytes = partitionData.slice(offset + 16, offset + 32);
      
      // Check for end marker (all zeros or all 0xFF)
      if (entryOffset === 0 && entrySize === 0 && type === 0 && subtype === 0) {
        // End of partition table
        break;
      }
      
      // Extract name - look for null terminator or 0xFF padding
      let nameEnd = 16;
      for (let i = 0; i < 16; i++) {
        if (nameBytes[i] === 0) {
          nameEnd = i;
          break;
        }
        // If we hit 0xFF padding, that's also the end (but 0xFF at position 0 might be valid padding)
        if (nameBytes[i] === 0xFF && i > 0) {
          nameEnd = i;
          break;
        }
      }
      
      const nameRaw = nameBytes.slice(0, nameEnd);
      
      // Validate name is printable ASCII
      let isValidName = nameRaw.length > 0 && nameRaw.length <= 16;
      for (let i = 0; i < nameRaw.length; i++) {
        const byte = nameRaw[i];
        // Allow printable ASCII (0x20-0x7E) - partition names are usually lowercase alphanumeric + underscore
        if (byte < 0x20 || byte > 0x7E) {
          isValidName = false;
          break;
        }
      }
      
      // Validate partition entry values
      // Entry offset should be aligned to 0x1000 (4KB) for ESP32-S3
      const isOffsetAligned = (entryOffset & 0xFFF) === 0;
      const isValidEntry = entryOffset !== 0xFFFFFFFF && 
                           entryOffset < 0x10000000 && 
                           entrySize !== 0xFFFFFFFF && 
                           entrySize < 0x10000000 &&
                           entrySize > 0 &&
                           entrySize < 0x8000000 && // Max 128MB
                           (type === 0x00 || type === 0x01) && // app or data
                           isValidName &&
                           isOffsetAligned; // ESP32-S3 requires 4KB alignment
      
      if (isValidEntry) {
        const name = nameRaw.toString('utf8').trim();
        if (name.length > 0) {
          candidates.push({ 
            name, 
            type, 
            subtype, 
            offset: entryOffset, 
            size: entrySize,
            tableOffset: offset
          });
          
          // Debug output
          if (event) {
            event.sender.send('flash-progress', `  Found partition: ${name} (type:0x${type.toString(16)}, subtype:0x${subtype.toString(16)}) at 0x${entryOffset.toString(16)}, size: 0x${entrySize.toString(16)}\n`);
          }
          
          // Check if this is SPIFFS partition
          // Subtype 0x82 = SPIFFS, or name contains "spiffs"
          const nameLower = name.toLowerCase();
          if (subtype === 0x82 || nameLower === 'spiffs' || nameLower.includes('spiffs')) {
            if (event) {
              event.sender.send('flash-progress', `✓ Identified SPIFFS partition: ${name} at 0x${entryOffset.toString(16)}\n`);
            }
            return `0x${entryOffset.toString(16)}`;
          }
        }
      }
      
      offset += 32;
    }
    
    // If we found valid partitions at this offset, this is likely the right table
    if (candidates.length > 0) {
      foundPartitions.push(...candidates);
      // If we found a good set of partitions, we can stop searching other offsets
      // (the first valid offset is usually correct)
      if (candidates.length >= 3) {
        break;
      }
    }
  }
  
  // If we found partitions but no SPIFFS, log them for debugging
  if (foundPartitions.length > 0 && event) {
    event.sender.send('flash-progress', `Found ${foundPartitions.length} partitions but no SPIFFS:\n`);
    foundPartitions.forEach(p => {
      event.sender.send('flash-progress', `  - ${p.name} (type:0x${p.type.toString(16)}, subtype:0x${p.subtype.toString(16)}) at 0x${p.offset.toString(16)}\n`);
    });
  }
  
  // If we found partitions but no SPIFFS, that's okay - return null to use default
  return null;
}

// Generate NVS partition with custom pin config
async function generateCustomNVS(customConfig, event = null) {
  const tempDir = app.getPath('temp');
  // Create temporary JSON file to pass config to NVS generation script
  const configJsonPath = path.join(tempDir, 'sfos_custom_config.json');
  const nvsImagePath = path.join(tempDir, 'custom_nvs.bin');
  
  // Write pin config as JSON (temporary file, used only by NVS generator)
  await fs.writeFile(configJsonPath, JSON.stringify(customConfig, null, 2));
  
  // Run Python script to generate NVS
  const scriptPath = path.join(__dirname, 'resources', 'scripts', 'generate_nvs.py');
  
  return new Promise((resolve, reject) => {
    // NVS partition size is 0x5000 (20KB) per partition table
    const python = spawn('python3', [scriptPath, configJsonPath, nvsImagePath, '0x5000']);
    
    let output = '';
    
    python.stdout.on('data', (data) => {
      const text = data.toString();
      output += text;
      console.log(text);
      if (event) {
        event.sender.send('flash-progress', text);
      }
    });
    
    python.stderr.on('data', (data) => {
      const text = data.toString();
      output += text;
      console.error(text);
      if (event) {
        event.sender.send('flash-progress', text);
      }
    });
    
    python.on('close', (code) => {
      if (code === 0) {
        resolve(nvsImagePath);
      } else {
        reject(new Error(`NVS generation failed: ${output}`));
      }
    });
    
    python.on('error', (err) => {
      // Try 'python' instead of 'python3'
      if (err.code === 'ENOENT') {
        const pythonAlt = spawn('python', [scriptPath, configJsonPath, nvsImagePath, '0x5000']);
        
        let altOutput = '';
        pythonAlt.stdout.on('data', (data) => {
          const text = data.toString();
          altOutput += text;
          console.log(text);
          if (event) {
            event.sender.send('flash-progress', text);
          }
        });
        pythonAlt.stderr.on('data', (data) => {
          const text = data.toString();
          altOutput += text;
          console.error(text);
          if (event) {
            event.sender.send('flash-progress', text);
          }
        });
        
        pythonAlt.on('close', (code) => {
          if (code === 0) {
            resolve(nvsImagePath);
          } else {
            reject(new Error(`NVS generation failed: ${altOutput}`));
          }
        });
        
        pythonAlt.on('error', (err2) => {
          reject(new Error(`Python not found: ${err2.message}`));
        });
      } else {
        reject(err);
      }
    });
  });
}

// Check if a path is a PlatformIO project
async function isPlatformIOProject(folderPath) {
  const platformioIni = path.join(folderPath, 'platformio.ini');
  try {
    await fs.access(platformioIni);
    return true;
  } catch {
    return false;
  }
}

// Find PlatformIO CLI command
function findPlatformIO() {
  // Determine platform-specific binary name
  let binaryName;
  if (process.platform === 'darwin') {
    binaryName = 'platformio-macos';
  } else if (process.platform === 'win32') {
    binaryName = 'platformio-win64.bat';  // Use .bat on Windows
  } else {
    binaryName = 'platformio-linux';
  }
  
  // Try bundled version first (production)
  if (app.isPackaged) {
    const bundledPath = path.join(process.resourcesPath, 'resources', 'bin', binaryName);
    if (require('fs').existsSync(bundledPath)) {
      return bundledPath;
    }
  }
  
  // Try development version (running from source)
  const devPath = path.join(__dirname, 'resources', 'bin', binaryName);
  if (require('fs').existsSync(devPath)) {
    return devPath;
  }
  
  // Fallback: try system PlatformIO (for development without bundled binaries)
  const commands = process.platform === 'win32' 
    ? ['pio', 'platformio', 'pio.exe', 'platformio.exe']
    : ['pio', 'platformio'];
  
  for (const cmd of commands) {
    try {
      require('child_process').execSync(`${cmd} --version`, { stdio: 'ignore' });
      return cmd;
    } catch (e) {
      // Command not found, try next
    }
  }
  
  return null;
}

// Flash firmware using esptool.py or PlatformIO
ipcMain.handle('flash-firmware', async (event, options) => {
  const { port, boardType, firmwarePath, baudRate = 921600, customConfig = null } = options;
  const config = BOARD_CONFIGS[boardType];
  
  if (!config) {
    throw new Error('Invalid board type');
  }
  
  // Check if this is a PlatformIO project (source code)
  const isPIOProject = await isPlatformIOProject(firmwarePath);
  
  if (isPIOProject) {
    // Use PlatformIO to build and upload
    return flashWithPlatformIO(event, firmwarePath, boardType, port, customConfig);
  }
  
  // Otherwise, use esptool with pre-built binaries
  return new Promise(async (resolve, reject) => {
    // Debug: Show what we're flashing
    event.sender.send('flash-progress', `\n=== Flashing pre-built firmware ===\n`);
    event.sender.send('flash-progress', `Board Type: ${boardType}\n`);
    event.sender.send('flash-progress', `Firmware Path: ${firmwarePath}\n`);
    event.sender.send('flash-progress', `Port: ${port}\n\n`);
    
    let esptoolCmd;
    try {
      esptoolCmd = findEsptool();
    } catch (error) {
      reject(error);
      return;
    }
    
    // Build esptool.py command
    const args = [
      '--chip', config.chip,
      '--port', port,
      '--baud', baudRate.toString(),
      '--before', 'default_reset',
      '--after', 'hard_reset',
      'write_flash'
    ];
    
    // Note: --flash_mode and --flash_size are not needed for write_flash
    // esptool auto-detects these from the chip and partition table
    // Adding them causes errors in some esptool versions
    
    // Add flash addresses and files
    const files = {
      bootloader: path.join(firmwarePath, 'bootloader.bin'),
      partitions: path.join(firmwarePath, 'partitions.bin'),
      firmware: path.join(firmwarePath, 'firmware.bin'),
      spiffs: path.join(firmwarePath, 'spiffs.bin')
    };
    
    // Try to read SPIFFS address from partitions.bin (more accurate than hardcoded)
    // This ensures we use the correct address from the actual partition table
    let spiffsAddress = config.flashAddresses.spiffs;
    if (require('fs').existsSync(files.partitions)) {
      try {
        // Read partition table to find SPIFFS address
        const partitionData = require('fs').readFileSync(files.partitions);
        event.sender.send('flash-progress', `Parsing partition table (${partitionData.length} bytes)...\n`);
        const spiffsAddr = findSPIFFSInPartitionTable(partitionData, event);
        if (spiffsAddr) {
          spiffsAddress = spiffsAddr;
          event.sender.send('flash-progress', `✓ Read SPIFFS address from partition table: ${spiffsAddress}\n`);
        } else {
          // Debug: Try to dump partition table entries for troubleshooting
          event.sender.send('flash-progress', `⚠ Could not find SPIFFS in partition table (${partitionData.length} bytes)\n`);
          // Try to list all partitions found for debugging
          let offset = 0;
          const foundPartitions = [];
          while (offset + 32 <= partitionData.length && foundPartitions.length < 10) {
            const nameBytes = partitionData.slice(offset, offset + 16);
            const name = nameBytes.toString('utf8').split('\0')[0].trim();
            if (name && name.length > 0 && name.charCodeAt(0) !== 0 && name.charCodeAt(0) !== 0xFF) {
              const entryOffset = partitionData.readUInt32LE(offset + 20);
              const subtype = partitionData.readUInt8(offset + 18);
              foundPartitions.push(`${name}@0x${entryOffset.toString(16)} (subtype:0x${subtype.toString(16)})`);
            }
            offset += 32;
            if (offset > 512) break;
          }
          if (foundPartitions.length > 0) {
            event.sender.send('flash-progress', `   Found partitions: ${foundPartitions.join(', ')}\n`);
          }
          event.sender.send('flash-progress', `   Using default SPIFFS address: ${spiffsAddress}\n`);
        }
      } catch (error) {
        event.sender.send('flash-progress', `⚠ Could not read partition table: ${error.message}, using default: ${spiffsAddress}\n`);
      }
    }
    
    // Check which files exist
    // IMPORTANT: Build file list in specific order to ensure SPIFFS is written last
    // This prevents any potential overwrite issues
    const fileOrder = ['bootloader', 'partitions', 'firmware', 'spiffs'];
    const existingFiles = [];
    const fileArgs = [];  // Separate array to control order
    
    // First pass: collect all files except SPIFFS
    fileOrder.forEach(key => {
      if (key === 'spiffs') return;  // Skip SPIFFS for now
      if (require('fs').existsSync(files[key])) {
        existingFiles.push(key);
        fileArgs.push(config.flashAddresses[key], files[key]);
        
      }
    });
    
    // SPIFFS will be uploaded separately using PlatformIO's uploadfs command
    // This ensures PlatformIO handles the correct SPIFFS address from the partition table
    let spiffsFile = null;
    if (require('fs').existsSync(files.spiffs)) {
      existingFiles.push('spiffs');
      spiffsFile = files.spiffs;
      
      // Check SPIFFS file size and warn if suspiciously small
      const stats = require('fs').statSync(files.spiffs);
      const sizeKB = stats.size / 1024;
      event.sender.send('flash-progress', `Found spiffs.bin (${sizeKB.toFixed(1)} KB)\n`);
      
      // Verify SPIFFS file is valid
      if (stats.size < 1024) {
        event.sender.send('flash-progress', `⚠️ WARNING: spiffs.bin is very small (${stats.size} bytes). It may be empty or incomplete.\n`);
      } else if (stats.size > 10 * 1024 * 1024) {
        event.sender.send('flash-progress', `⚠️ WARNING: spiffs.bin is unusually large (${sizeKB.toFixed(1)} KB). This may indicate corruption.\n`);
      } else {
        // Try to read first few bytes to verify it's not all zeros or corrupted
        const fs = require('fs');
        const buffer = Buffer.alloc(256);
        const fd = fs.openSync(files.spiffs, 'r');
        const bytesRead = fs.readSync(fd, buffer, 0, 256, 0);
        fs.closeSync(fd);
        
        // Check if file is all zeros (common corruption pattern)
        const allZeros = buffer.slice(0, bytesRead).every(byte => byte === 0);
        if (allZeros && bytesRead > 0) {
          event.sender.send('flash-progress', `⚠️ WARNING: spiffs.bin appears to be empty (all zeros). File may be corrupted.\n`);
        } else {
          event.sender.send('flash-progress', `✓ spiffs.bin appears valid (${sizeKB.toFixed(1)} KB)\n`);
        }
      }
      
      event.sender.send('flash-progress', `✓ SPIFFS will be uploaded using PlatformIO (ensures correct address from partition table)\n`);
    } else {
      event.sender.send('flash-progress', `⚠️ WARNING: spiffs.bin not found in firmware package. SPIFFS filesystem will not be uploaded.\n`);
      event.sender.send('flash-progress', `   This means web files (index.html, etc.) will not be available.\n`);
    }
    
    // Add all file arguments to the command
    args.push(...fileArgs);
    
    // If no firmware.bin found, that's an error
    if (!existingFiles.includes('firmware')) {
      reject(new Error('No firmware files found, expected firmware.bin. If this is source code, make sure platformio.ini exists in the folder.'));
      return;
    }
    
    // Warn if SPIFFS is missing
    if (!existingFiles.includes('spiffs')) {
      event.sender.send('flash-progress', `\n⚠️ NOTE: No SPIFFS filesystem will be uploaded.\n`);
      event.sender.send('flash-progress', `   The device will work, but web interface files may be missing.\n`);
      event.sender.send('flash-progress', `   To include SPIFFS, flash from source code or ensure the release includes spiffs.bin.\n\n`);
    }
    
    // If custom config is provided, generate and add custom NVS
    if (customConfig) {
      try {
        event.sender.send('flash-progress', '\n=== Generating custom pin config NVS ===\n');
        const customNVSPath = await generateCustomNVS(customConfig, event);
        event.sender.send('flash-progress', `✓ Custom NVS generated: ${customNVSPath}\n`);
        
        // Add NVS to flash command (before firmware to preserve it)
        // NVS address is always 0x9000 per partition table
        const nvsAddress = config.flashAddresses.nvs || '0x9000';
        fileArgs.push(nvsAddress, customNVSPath);
        event.sender.send('flash-progress', `✓ Custom NVS will be written at ${nvsAddress} (pin configuration)\n`);
      } catch (error) {
        event.sender.send('flash-progress', `⚠ Custom NVS generation failed: ${error.message}\n`);
        event.sender.send('flash-progress', 'Continuing with standard firmware (no custom pins)\n');
        event.sender.send('flash-progress', `Note: Install ESP-IDF or set IDF_PATH for NVS generation\n`);
      }
    }
    
    // Log the exact command being run for debugging
    event.sender.send('flash-progress', `\n=== Flashing files ===\n`);
    event.sender.send('flash-progress', `Command: ${esptoolCmd} ${args.join(' ')}\n`);
    event.sender.send('flash-progress', `Files to flash:\n`);
    for (let i = 6; i < args.length; i += 2) {
      if (i + 1 < args.length) {
        const addr = args[i];
        const file = args[i + 1];
        const fileName = path.basename(file);
        event.sender.send('flash-progress', `  ${addr}: ${fileName}\n`);
      }
    }
    event.sender.send('flash-progress', `\n`);
    
    // Spawn esptool
    const esptool = spawn(esptoolCmd, args);
    
    let output = '';
    
    esptool.stdout.on('data', (data) => {
      const text = data.toString();
      output += text;
      event.sender.send('flash-progress', text);
      
      // Removed progress percentage parsing - esptool percentages are also unreliable
      // Users can see progress in the console output instead
    });
    
    esptool.stderr.on('data', (data) => {
      const text = data.toString();
      output += text;
      event.sender.send('flash-progress', text);
    });
    
    esptool.on('close', async (code) => {
      if (code === 0) {
        // If SPIFFS file exists, upload it using PlatformIO's uploadfs command
        // This ensures PlatformIO reads the correct SPIFFS address from the partition table
        if (spiffsFile && require('fs').existsSync(spiffsFile)) {
          try {
            event.sender.send('flash-progress', `\n=== Uploading SPIFFS ===\n`);
            event.sender.send('flash-progress', `SPIFFS file: ${spiffsFile}\n`);
            event.sender.send('flash-progress', `Board type: ${boardType}\n`);
            await uploadSPIFFSFromPrebuilt(event, firmwarePath, boardType, port, spiffsFile);
            event.sender.send('flash-progress', `\n✅ SPIFFS uploaded successfully\n`);
          } catch (spiffsError) {
            event.sender.send('flash-progress', `\n⚠️ WARNING: SPIFFS upload failed: ${spiffsError.message}\n`);
            event.sender.send('flash-progress', `   Error details: ${spiffsError.stack || spiffsError.toString()}\n`);
            event.sender.send('flash-progress', `   Firmware was flashed successfully, but SPIFFS may not be available.\n`);
            event.sender.send('flash-progress', `   You may need to manually upload SPIFFS using PlatformIO.\n`);
            // Don't fail the entire flash - firmware is already uploaded
          }
        } else {
          if (spiffsFile) {
            event.sender.send('flash-progress', `\n⚠️ SPIFFS file not found: ${spiffsFile}\n`);
          }
        }
        
        resolve({ success: true, output });
      } else {
        reject(new Error(`Flashing failed with code ${code}\n${output}`));
      }
    });
    
    esptool.on('error', (err) => {
      reject(new Error(`Failed to start esptool: ${err.message}`));
    });
  });
});

// Flash using PlatformIO CLI
async function flashWithPlatformIO(event, projectPath, boardType, port, customConfig) {
  return new Promise(async (resolve, reject) => {
    const pioCmd = findPlatformIO();
    
    if (!pioCmd) {
      reject(new Error('PlatformIO CLI not found. Please install PlatformIO:\n  pip install platformio\n  or\n  pip install --user platformio'));
      return;
    }
    
    // Map board type to PlatformIO environment name
    const envMap = {
      'esp32-c3-supermini': 'esp32-c3-supermini',
    'nuclearcounter': 'nuclearcounter',
      'esp32-c3': 'esp32-c3',
      'esp32-c6': 'esp32-c6',
      'esp32dev': 'esp32dev',
      'esp32dev-2mb': 'esp32dev-2mb',
      'esp32-s3': 'esp32-s3',
      'esp32-s3-touch': 'esp32-s3-touch',
      'esp32-s3-t-energy': 'esp32-s3-t-energy',
      'esp32-s2': 'esp32-s2',
      'jc2432w328c': 'jc2432w328c'
    };
    
    const envName = envMap[boardType];
    if (!envName) {
      reject(new Error(`Unknown board type: ${boardType}`));
      return;
    }
    
    // Build and upload firmware
    event.sender.send('flash-progress', `\n=== Building and uploading with PlatformIO ===\n`);
    event.sender.send('flash-progress', `Board Type: ${boardType}\n`);
    event.sender.send('flash-progress', `Environment: ${envName}\n`);
    event.sender.send('flash-progress', `Port: ${port}\n\n`);
    
    // Handle UNC paths on Windows (\\wsl.localhost\... paths)
    // CMD.EXE doesn't support UNC paths as working directories
    let workingDir = projectPath;
    let useShell = process.platform === 'win32';
    
    if (process.platform === 'win32' && projectPath.startsWith('\\\\')) {
      // UNC path - convert to forward slashes for better compatibility
      // Node.js can handle forward slashes on Windows
      workingDir = projectPath.replace(/\\/g, '/');
      event.sender.send('flash-progress', `Note: Converting UNC path for Windows compatibility\n`);
      // Use shell: true but with cmd /c to handle UNC paths better
      useShell = true;
    } else if (process.platform === 'win32') {
      // Regular Windows path - convert to forward slashes for Node.js
      workingDir = projectPath.replace(/\\/g, '/');
    }
    
    // PlatformIO uses PLATFORMIO_UPLOAD_PORT environment variable, not -p flag
    const env = { ...process.env };
    env.PLATFORMIO_UPLOAD_PORT = port;
    
    // Fix Unicode encoding issues on Windows and enable unbuffered output
    if (process.platform === 'win32') {
      env.PYTHONIOENCODING = 'utf-8';
      env.PYTHONUTF8 = '1';
      // Also set console codepage to UTF-8
      env.CHCP = '65001';
    }
    // Enable unbuffered Python output for real-time console updates (all platforms)
    env.PYTHONUNBUFFERED = '1';
    // Force PlatformIO to use unbuffered output
    env.PLATFORMIO_FORCE_UNBUFFERED = '1';
    
    // On Windows, try to remove specific problematic files that commonly get locked
    if (process.platform === 'win32') {
      try {
        const fs = require('fs');
        const path = require('path');
        const buildDir = path.join(workingDir, '.pio', 'build', envName);
        const problematicFiles = [
          'bootloader.bin',
          'partitions.bin',
          'firmware.bin'
        ];
        
        for (const file of problematicFiles) {
          const filePath = path.join(buildDir, file);
          try {
            if (fs.existsSync(filePath)) {
              // Try to delete with a small delay between attempts
              for (let i = 0; i < 3; i++) {
                try {
                  fs.unlinkSync(filePath);
                  break; // Success, exit retry loop
                } catch (err) {
                  if (i < 2) {
                    await new Promise(resolve => setTimeout(resolve, 500)); // Wait 500ms and retry
                  }
                }
              }
            }
          } catch (err) {
            // Ignore errors - file might not exist or be locked
          }
        }
      } catch (err) {
        // Ignore errors
      }
    }
    
    // Clean build directory first to avoid file locking issues on Windows
    // This ensures build flags are properly applied and no stale artifacts remain
    event.sender.send('flash-progress', 'Cleaning previous build artifacts...\n');
    const cleanArgs = ['run', '-e', envName, '-t', 'clean', '--verbose'];
    const cleanPio = spawn(pioCmd, cleanArgs, {
      cwd: workingDir,
      env: env,
      shell: useShell,
      stdio: ['ignore', 'pipe', 'pipe'] // Explicitly pipe stdout/stderr for real-time output
    });
    
    // Capture clean output (but don't show it unless there's an error)
    let cleanOutput = '';
    cleanPio.stdout?.on('data', (data) => {
      cleanOutput += data.toString();
    });
    cleanPio.stderr?.on('data', (data) => {
      cleanOutput += data.toString();
    });
    
    // Wait for clean to complete (don't fail if it errors - might be first build)
    await new Promise((resolve) => {
      cleanPio.on('close', (code) => {
        if (code === 0) {
          event.sender.send('flash-progress', '✓ Build directory cleaned\n');
        }
        resolve();
      });
      cleanPio.on('error', () => {
        // Ignore errors, continue anyway (might be first build)
        resolve();
      });
      // Set timeout to prevent hanging
      setTimeout(() => {
        event.sender.send('flash-progress', '⚠ Clean step timed out, continuing...\n');
        resolve();
      }, 15000); // 15 second timeout
    });
    
    // Give Windows time to release file locks after clean
    // This is especially important on Windows where file locks can persist briefly
    event.sender.send('flash-progress', 'Waiting for file locks to release...\n');
    await new Promise(resolve => setTimeout(resolve, 2000)); // 2 second delay
    
    // On all platforms, try to manually remove the build directory if it exists
    // This ensures a completely clean build and that build flags are properly applied
    // This is especially important on Windows where file locks can persist
    const fs = require('fs');
    const path = require('path');
    const buildDir = path.join(workingDir, '.pio', 'build', envName);
    try {
      if (fs.existsSync(buildDir)) {
        // Try to remove the directory (will fail silently if locked, that's OK)
        try {
          fs.rmSync(buildDir, { recursive: true, force: true, maxRetries: 3, retryDelay: 500 });
          event.sender.send('flash-progress', '✓ Build directory removed (ensures clean build with correct flags)\n');
        } catch (err) {
          // Ignore errors - files might still be locked, PlatformIO clean will handle it
          event.sender.send('flash-progress', '⚠ Some files still locked, PlatformIO will handle cleanup\n');
        }
        // Give it another moment after manual removal
        await new Promise(resolve => setTimeout(resolve, 1000));
      }
    } catch (err) {
      // Ignore errors, continue with build
    }
    
    const buildArgs = ['run', '-e', envName, '-t', 'upload', '--verbose'];
    
    // Debug: Show exact command being run
    event.sender.send('flash-progress', `Running: ${pioCmd} ${buildArgs.join(' ')}\n`);
    event.sender.send('flash-progress', `Working directory: ${workingDir}\n`);
    event.sender.send('flash-progress', `Environment: PLATFORMIO_UPLOAD_PORT=${port}\n\n`);
    
    // On Windows with batch files, we need shell: true
    // But we'll use the workingDir with forward slashes which Node.js handles
    // Use explicit stdio configuration to ensure real-time output
    const pio = spawn(pioCmd, buildArgs, {
      cwd: workingDir,
      env: env,
      shell: useShell,
      stdio: ['ignore', 'pipe', 'pipe'] // Explicitly pipe stdout/stderr for real-time output
    });
    
    let output = '';
    
    pio.stdout.on('data', (data) => {
      const text = data.toString();
      output += text;
      event.sender.send('flash-progress', text);
      // Removed progress percentage parsing - PlatformIO outputs multiple percentages
      // (build, upload, memory) that conflict, making the progress bar inaccurate
    });
    
    pio.stderr.on('data', (data) => {
      const text = data.toString();
      output += text;
      event.sender.send('flash-progress', text);
    });
    
    pio.on('close', (code) => {
      if (code === 0) {
        // Always upload SPIFFS after successful firmware upload
        // Upload SPIFFS (web files) and NVS (pin config) separately
        if (customConfig) {
          // First upload SPIFFS (web files only: index.html, style.css, app.js from data/)
          event.sender.send('flash-progress', '\n=== Uploading SPIFFS filesystem (web files) ===\n');
          uploadSPIFFS(event, projectPath, envName, port, pioCmd, env, useShell, workingDir)
            .then(() => {
              // Then upload NVS with pin config
              event.sender.send('flash-progress', '\n=== Uploading custom pin config to NVS ===\n');
              return uploadCustomNVSToDevice(event, port, customConfig, boardType);
            })
            .then(() => {
              resolve({ success: true, output });
            })
            .catch((error) => {
              event.sender.send('flash-progress', `⚠ Upload failed: ${error.message}\n`);
              event.sender.send('flash-progress', 'Firmware uploaded successfully, but custom config may not be applied.\n');
              resolve({ success: true, output, warning: 'Config upload failed' });
            });
        } else {
          // Upload standard SPIFFS (includes all files from data/ directory)
          event.sender.send('flash-progress', '\n=== Uploading SPIFFS filesystem ===\n');
          uploadSPIFFS(event, projectPath, envName, port, pioCmd, env, useShell, workingDir)
            .then(() => {
              resolve({ success: true, output });
            })
            .catch((error) => {
              event.sender.send('flash-progress', `⚠ SPIFFS upload failed: ${error.message}\n`);
              event.sender.send('flash-progress', 'Firmware uploaded successfully, but SPIFFS files not uploaded.\n');
              resolve({ success: true, output, warning: 'SPIFFS upload failed' });
            });
        }
      } else {
        reject(new Error(`PlatformIO build/upload failed with code ${code}\n${output}`));
      }
    });
    
    pio.on('error', (err) => {
      reject(new Error(`Failed to start PlatformIO: ${err.message}`));
    });
  });
}

// Upload SPIFFS using PlatformIO uploadfs (standard upload, no custom config)
// This ensures all files from data/ directory are included (index.html, style.css, app.js, etc.)
async function uploadSPIFFS(event, projectPath, envName, port, pioCmd, env, useShell, workingDir) {
  try {
    // Set upload port for PlatformIO
    const uploadEnv = { ...env };
    uploadEnv.PLATFORMIO_UPLOAD_PORT = port;
    
    // Run PlatformIO uploadfs to upload SPIFFS (includes all files from data/ directory)
    event.sender.send('flash-progress', 'Uploading SPIFFS filesystem (includes all web files)...\n');
    return new Promise((resolve, reject) => {
      const uploadfsArgs = ['run', '-e', envName, '-t', 'uploadfs', '--verbose'];
      const pio = spawn(pioCmd, uploadfsArgs, {
        cwd: workingDir,
        env: uploadEnv,
        shell: useShell,
        stdio: ['ignore', 'pipe', 'pipe']
      });
      
      let output = '';
      
      pio.stdout.on('data', (data) => {
        const text = data.toString();
        output += text;
        event.sender.send('flash-progress', text);
      });
      
      pio.stderr.on('data', (data) => {
        const text = data.toString();
        output += text;
        event.sender.send('flash-progress', text);
      });
      
      pio.on('close', (code) => {
        if (code === 0) {
          event.sender.send('flash-progress', '✓ SPIFFS uploaded successfully (all web files included)\n');
          resolve();
        } else {
          reject(new Error(`SPIFFS upload failed with code ${code}\n${output}`));
        }
      });
      
      pio.on('error', (err) => {
        reject(new Error(`Failed to start SPIFFS upload: ${err.message}`));
      });
    });
  } catch (error) {
    throw new Error(`Failed to prepare SPIFFS upload: ${error.message}`);
  }
}

// Extract files from SPIFFS binary using Node.js (self-contained, no Python)
function extractSPIFFSFiles(spiffsBinPath, extractDir) {
  const fs = require('fs');
  const spiffsData = fs.readFileSync(spiffsBinPath);
  
  // Ensure extract directory exists
  if (!fs.existsSync(extractDir)) {
    fs.mkdirSync(extractDir, { recursive: true });
  }
  
  const extractedFiles = [];
  
  // Extract by content signatures (same method as Python script)
  // Look for HTML, CSS, JS files by their content markers
  
  // Find index.html
  const htmlMarkers = [Buffer.from('<!DOCTYPE html'), Buffer.from('<html'), Buffer.from('<HTML')];
  for (const marker of htmlMarkers) {
    const idx = spiffsData.indexOf(marker);
    if (idx !== -1) {
      // Find the end (look for </html> or end of non-zero data)
      let endIdx = spiffsData.indexOf(Buffer.from('</html>'), idx);
      if (endIdx === -1) {
        // Find end of non-zero block
        endIdx = idx;
        let consecutiveEmpty = 0;
        while (endIdx < spiffsData.length) {
          const byte = spiffsData[endIdx];
          if (byte === 0 || byte === 0xFF) {
            consecutiveEmpty++;
            if (consecutiveEmpty > 256) break;
          } else {
            consecutiveEmpty = 0;
          }
          endIdx++;
        }
        endIdx -= consecutiveEmpty;
      } else {
        endIdx += 7; // Include </html>
      }
      
      const content = spiffsData.slice(idx, endIdx);
      if (content.length > 50) {
        fs.writeFileSync(path.join(extractDir, 'index.html'), content);
        extractedFiles.push('index.html');
        break;
      }
    }
  }
  
  // Find style.css
  const cssMarkers = [Buffer.from('body {'), Buffer.from('body{'), Buffer.from('/*'), Buffer.from('@media')];
  for (const marker of cssMarkers) {
    const idx = spiffsData.indexOf(marker);
    if (idx !== -1 && idx > 100) {
      let endIdx = idx;
      let consecutiveEmpty = 0;
      while (endIdx < spiffsData.length) {
        const byte = spiffsData[endIdx];
        if (byte === 0 || byte === 0xFF) {
          consecutiveEmpty++;
          if (consecutiveEmpty > 256) break;
        } else {
          consecutiveEmpty = 0;
        }
        endIdx++;
      }
      endIdx -= consecutiveEmpty;
      
      const content = spiffsData.slice(idx, endIdx);
      if (content.length > 10) {
        fs.writeFileSync(path.join(extractDir, 'style.css'), content);
        extractedFiles.push('style.css');
        break;
      }
    }
  }
  
  // Find app.js
  const jsMarkers = [Buffer.from('console.'), Buffer.from('function '), Buffer.from('const '), Buffer.from('let ')];
  for (const marker of jsMarkers) {
    const idx = spiffsData.indexOf(marker);
    if (idx !== -1 && idx > 100) {
      let endIdx = idx;
      let consecutiveEmpty = 0;
      while (endIdx < spiffsData.length) {
        const byte = spiffsData[endIdx];
        if (byte === 0 || byte === 0xFF) {
          consecutiveEmpty++;
          if (consecutiveEmpty > 256) break;
        } else {
          consecutiveEmpty = 0;
        }
        endIdx++;
      }
      endIdx -= consecutiveEmpty;
      
      const content = spiffsData.slice(idx, endIdx);
      if (content.length > 10) {
        fs.writeFileSync(path.join(extractDir, 'app.js'), content);
        extractedFiles.push('app.js');
        break;
      }
    }
  }
  
  return extractedFiles;
}

// Read SPIFFS address from device using esptool (after partition table is flashed)
async function readSPIFFSAddressFromDevice(event, chip, port, partitionsAddress = '0x8000') {
  const esptoolCmd = findEsptool();
  if (!esptoolCmd) {
    return null;
  }
  
  // Try reading different sizes - ESP-IDF partition tables can vary
  // Default is usually 0xC00 (3072 bytes) but actual entries are smaller
  // Try reading the full 3072 bytes first, then try smaller sizes
  const sizesToTry = [3072, 1024, 640, 512];
  
  for (const partitionTableSize of sizesToTry) {
    const result = await new Promise((resolve) => {
      const tempPartitionFile = require('path').join(require('os').tmpdir(), `partitions-read-${Date.now()}.bin`);
      const args = [
        '--chip', chip,
        '--port', port,
        '--baud', '921600',
        'read_flash',
        partitionsAddress,
        `0x${partitionTableSize.toString(16)}`,
        tempPartitionFile
      ];
      
      event.sender.send('flash-progress', `Reading partition table from device at ${partitionsAddress} (${partitionTableSize} bytes)...\n`);
      
      const esptool = spawn(esptoolCmd, args);
      let output = '';
      
      esptool.stdout.on('data', (data) => {
        output += data.toString();
      });
      esptool.stderr.on('data', (data) => {
        output += data.toString();
      });
      
      esptool.on('close', (code) => {
        if (code === 0 && require('fs').existsSync(tempPartitionFile)) {
          try {
            const partitionData = require('fs').readFileSync(tempPartitionFile);
            event.sender.send('flash-progress', `Read ${partitionData.length} bytes from device\n`);
            
            // Try parsing with different starting offsets
            // ESP-IDF partition tables may have MD5 checksum (16 bytes) at start
            for (let offset = 0; offset < Math.min(128, partitionData.length - 32); offset += 16) {
              const spiffsAddress = findSPIFFSInPartitionTable(partitionData.slice(offset), event);
              if (spiffsAddress) {
                // Clean up temp file
                try {
                  require('fs').unlinkSync(tempPartitionFile);
                } catch (e) {}
                event.sender.send('flash-progress', `✓ Found SPIFFS at ${spiffsAddress} from device (offset ${offset})\n`);
                resolve(spiffsAddress);
                return;
              }
            }
            
            // Clean up temp file
            try {
              require('fs').unlinkSync(tempPartitionFile);
            } catch (e) {}
            event.sender.send('flash-progress', `⚠ Could not find SPIFFS in device partition table (tried ${partitionTableSize} bytes)\n`);
            resolve(null);
          } catch (err) {
            event.sender.send('flash-progress', `⚠ Error parsing device partition table: ${err.message}\n`);
            resolve(null);
          }
        } else {
          if (code !== 0) {
            event.sender.send('flash-progress', `⚠ Could not read partition table from device (code ${code}, tried ${partitionTableSize} bytes)\n`);
          }
          resolve(null);
        }
      });
      
      esptool.on('error', (err) => {
        event.sender.send('flash-progress', `⚠ Error reading from device: ${err.message}\n`);
        resolve(null);
      });
    });
    
    // If we found the address, return it
    if (result) {
      return result;
    }
  }
  
  return null;
}

// Upload SPIFFS from pre-built spiffs.bin using esptool directly (self-contained)
async function uploadSPIFFSFromPrebuilt(event, firmwarePath, boardType, port, spiffsBinPath) {
  const esptoolCmd = findEsptool();
  if (!esptoolCmd) {
    throw new Error('esptool not found. Cannot upload SPIFFS.');
  }
  
  // Get chip type from board config
  const config = BOARD_CONFIGS[boardType];
  if (!config) {
    throw new Error(`Unknown board type: ${boardType}`);
  }
  const chip = config.chip;
  
  // Try to read SPIFFS address from partition table on device
  // (partition table should already be flashed at this point)
  event.sender.send('flash-progress', 'Reading SPIFFS address from device partition table...\n');
  let spiffsAddress = await readSPIFFSAddressFromDevice(event, chip, port);
  
  // If reading from device failed, try parsing partitions.bin file
  if (!spiffsAddress) {
    const partitionsPath = path.join(firmwarePath, 'partitions.bin');
    if (require('fs').existsSync(partitionsPath)) {
      event.sender.send('flash-progress', 'Parsing partition table from firmware package...\n');
      try {
        const partitionData = require('fs').readFileSync(partitionsPath);
        spiffsAddress = findSPIFFSInPartitionTable(partitionData, event);
        if (spiffsAddress) {
          event.sender.send('flash-progress', `✓ Found SPIFFS address: ${spiffsAddress}\n`);
        }
      } catch (error) {
        event.sender.send('flash-progress', `⚠ Could not parse partition table: ${error.message}\n`);
      }
    }
  } else {
    event.sender.send('flash-progress', `✓ Read SPIFFS address from device: ${spiffsAddress}\n`);
  }
  
  // Fallback to default if partition table parsing failed
  if (!spiffsAddress) {
    // Use default from config (which is now correct for ESP32-S3)
    spiffsAddress = config.flashAddresses.spiffs;
    event.sender.send('flash-progress', `⚠ Using default SPIFFS address: ${spiffsAddress}\n`);
    if (boardType === 'esp32-s3' || boardType === 'esp32-s3-touch' || boardType === 'esp32-s3-t-energy') {
      event.sender.send('flash-progress', `   NOTE: ESP32-S3 default is 0x190000 (matches PlatformIO default partition table).\n`);
      event.sender.send('flash-progress', `   Common ESP32-S3 SPIFFS addresses: 0x190000 (default), 0x290000 (legacy), 0x3D0000 (custom)\n`);
    }
    event.sender.send('flash-progress', `   If SPIFFS doesn't work, try using 'Local Folder' method.\n`);
  }
  
  // Use esptool directly to upload SPIFFS
  event.sender.send('flash-progress', `Uploading SPIFFS to ${spiffsAddress} using esptool...\n`);
  
  const args = [
    '--chip', chip,
    '--port', port,
    '--baud', '921600',
    '--before', 'default_reset',
    '--after', 'hard_reset',
    'write_flash',
    spiffsAddress,
    spiffsBinPath
  ];
  
  return new Promise((resolve, reject) => {
    const esptool = spawn(esptoolCmd, args);
    
    let output = '';
    esptool.stdout.on('data', (data) => {
      const text = data.toString();
      output += text;
      event.sender.send('flash-progress', text);
    });
    
    esptool.stderr.on('data', (data) => {
      const text = data.toString();
      output += text;
      event.sender.send('flash-progress', text);
    });
    
    esptool.on('close', (code) => {
      if (code === 0) {
        // Check output for confirmation that SPIFFS was written
        if (output.includes('Wrote') && output.includes(spiffsAddress)) {
          event.sender.send('flash-progress', '✓ SPIFFS uploaded successfully\n');
          resolve();
        } else {
          event.sender.send('flash-progress', '⚠ SPIFFS upload completed, but verification unclear\n');
          resolve(); // Still resolve - esptool returned 0
        }
      } else {
        reject(new Error(`SPIFFS upload failed with code ${code}\n${output}`));
      }
    });
    
    esptool.on('error', (err) => {
      reject(new Error(`Failed to start SPIFFS upload: ${err.message}`));
    });
  });
}

// Upload custom NVS to device using esptool
async function uploadCustomNVSToDevice(event, port, customConfig, boardType) {
  // Generate NVS image
  event.sender.send('flash-progress', 'Generating custom NVS image...\n');
  const nvsPath = await generateCustomNVS(customConfig, event);
  
  // Use esptool to upload NVS to the correct address
  const config = BOARD_CONFIGS[boardType];
  
  if (!config) {
    throw new Error('Could not determine board config for NVS upload');
  }
  
  let esptoolCmd;
  try {
    esptoolCmd = findEsptool();
  } catch (error) {
    throw new Error('esptool not found for NVS upload');
  }
  
  const nvsAddress = config.flashAddresses.nvs || '0x9000';
  
  return new Promise((resolve, reject) => {
    const args = [
      '--chip', config.chip,
      '--port', port,
      '--baud', '921600',
      '--before', 'default_reset',
      '--after', 'hard_reset',
      'write_flash',
      nvsAddress,
      nvsPath
    ];
    
    event.sender.send('flash-progress', `Uploading NVS to ${nvsAddress}...\n`);
    
    const esptool = spawn(esptoolCmd, args);
    
    let output = '';
    
    esptool.stdout.on('data', (data) => {
      const text = data.toString();
      output += text;
      event.sender.send('flash-progress', text);
    });
    
    esptool.stderr.on('data', (data) => {
      const text = data.toString();
      output += text;
      event.sender.send('flash-progress', text);
    });
    
    esptool.on('close', (code) => {
      if (code === 0) {
        event.sender.send('flash-progress', '✓ NVS uploaded successfully (custom pin config)\n');
        resolve();
      } else {
        reject(new Error(`NVS upload failed with code ${code}\n${output}`));
      }
    });
    
    esptool.on('error', (err) => {
      reject(new Error(`Failed to start NVS upload: ${err.message}`));
    });
  });
}

// Upload custom NVS using esptool (fallback - same as uploadCustomNVSToDevice)
// Kept for backwards compatibility, but redirects to new function
async function uploadCustomSPIFFS(event, projectPath, envName, port, customConfig, boardType) {
  // This function name is misleading - it actually uploads NVS now, not SPIFFS
  // SPIFFS should be uploaded separately for web files
  return uploadCustomNVSToDevice(event, port, customConfig, boardType);
}

// Erase flash - only erase app and SPIFFS partitions, preserve NVS and factory data
ipcMain.handle('erase-flash', async (event, options) => {
  const { port, boardType, fullErase = false } = options;
  const config = BOARD_CONFIGS[boardType];
  
  return new Promise((resolve, reject) => {
    let esptoolCmd;
    try {
      esptoolCmd = findEsptool();
    } catch (error) {
      reject(error);
      return;
    }
    
    if (fullErase) {
      // Full chip erase (erases everything including NVS, factory data, etc.)
      // Use with caution - will lose WiFi calibration, MAC address, etc.
      event.sender.send('erase-progress', '⚠️ Performing FULL chip erase (all data will be lost)...\n');
      const args = [
        '--chip', config.chip,
        '--port', port,
        'erase_flash'
      ];
      
      const esptool = spawn(esptoolCmd, args);
      let output = '';
      
      esptool.stdout.on('data', (data) => {
        output += data.toString();
        event.sender.send('erase-progress', data.toString());
      });
      
      esptool.stderr.on('data', (data) => {
        output += data.toString();
      });
      
      esptool.on('close', (code) => {
        if (code === 0) {
          resolve({ success: true, output });
        } else {
          reject(new Error(`Erase failed with code ${code}\n${output}`));
        }
      });
    } else {
      // Selective erase - only erase app and SPIFFS partitions
      // Preserves NVS (WiFi calibration, MAC address) and factory data
      event.sender.send('erase-progress', 'Erasing app and SPIFFS partitions (preserving NVS and factory data)...\n');
      
      // Erase app partition (typically starts at 0x10000, size varies)
      // Erase SPIFFS partition (address from config, typically 0x290000)
      const appStart = config.flashAddresses.firmware;
      const spiffsStart = config.flashAddresses.spiffs;
      
      // Default partition sizes (these are typical, actual may vary)
      // App partition: typically 1.5MB (0x180000)
      // SPIFFS partition: typically 1.5MB (0x180000)
      const appSize = '0x180000';  // 1.5MB
      const spiffsSize = '0x180000';  // 1.5MB
      
      event.sender.send('erase-progress', `Erasing app partition: ${appStart} (${appSize})\n`);
      event.sender.send('erase-progress', `Erasing SPIFFS partition: ${spiffsStart} (${spiffsSize})\n`);
      
      // Erase app partition
      const eraseApp = spawn(esptoolCmd, [
        '--chip', config.chip,
        '--port', port,
        'erase_region', appStart, appSize
      ]);
      
      let appOutput = '';
      eraseApp.stdout.on('data', (data) => {
        appOutput += data.toString();
        event.sender.send('erase-progress', data.toString());
      });
      eraseApp.stderr.on('data', (data) => {
        appOutput += data.toString();
      });
      
      eraseApp.on('close', (appCode) => {
        if (appCode !== 0) {
          reject(new Error(`Failed to erase app partition: ${appOutput}`));
          return;
        }
        
        // Erase SPIFFS partition
        event.sender.send('erase-progress', `✓ App partition erased\n`);
        const eraseSPIFFS = spawn(esptoolCmd, [
          '--chip', config.chip,
          '--port', port,
          'erase_region', spiffsStart, spiffsSize
        ]);
        
        let spiffsOutput = '';
        eraseSPIFFS.stdout.on('data', (data) => {
          spiffsOutput += data.toString();
          event.sender.send('erase-progress', data.toString());
        });
        eraseSPIFFS.stderr.on('data', (data) => {
          spiffsOutput += data.toString();
        });
        
        eraseSPIFFS.on('close', (spiffsCode) => {
          if (spiffsCode === 0) {
            event.sender.send('erase-progress', `✓ SPIFFS partition erased\n`);
            event.sender.send('erase-progress', `✓ Erase complete (NVS and factory data preserved)\n`);
            resolve({ success: true, output: appOutput + spiffsOutput });
          } else {
            reject(new Error(`Failed to erase SPIFFS partition: ${spiffsOutput}`));
          }
        });
      });
    }
  });
});

// Select local firmware folder
ipcMain.handle('select-firmware-folder', async () => {
  const result = await dialog.showOpenDialog(mainWindow, {
    properties: ['openDirectory'],
    title: 'Select Firmware Folder'
  });
  
  if (result.canceled) {
    return null;
  }
  
  return result.filePaths[0];
});

