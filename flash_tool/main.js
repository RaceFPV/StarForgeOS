const { app, BrowserWindow, ipcMain, dialog } = require('electron');
const path = require('path');
const { spawn } = require('child_process');
const fs = require('fs').promises;
const { SerialPort } = require('serialport');
const axios = require('axios');
const extractZip = require('extract-zip');

let mainWindow;

// Board configurations matching platformio.ini
const BOARD_CONFIGS = {
  'esp32-c3-supermini': {
    name: 'ESP32-C3 SuperMini',
    chip: 'esp32c3',
    flashAddresses: {
      bootloader: '0x0',
      partitions: '0x8000',
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
      firmware: '0x10000',
      spiffs: '0x290000'
    }
  },
  'esp32dev': {
    name: 'ESP32 Dev Module',
    chip: 'esp32',
    flashAddresses: {
      bootloader: '0x1000',
      partitions: '0x8000',
      firmware: '0x10000',
      spiffs: '0x290000'
    }
  },
  'esp32-s3': {
    name: 'ESP32-S3 Dev Module',
    chip: 'esp32s3',
    flashAddresses: {
      bootloader: '0x0',
      partitions: '0x8000',
      firmware: '0x10000',
      spiffs: '0x290000'
    }
  },
  'esp32-s3-touch': {
    name: 'ESP32-S3 Touch LCD (no PSRAM)',
    chip: 'esp32s3',
    flashAddresses: {
      bootloader: '0x0',
      partitions: '0x8000',
      firmware: '0x10000',
      spiffs: '0x290000'
    }
  },
  'esp32-s2': {
    name: 'ESP32-S2 Dev Module',
    chip: 'esp32s2',
    flashAddresses: {
      bootloader: '0x1000',
      partitions: '0x8000',
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
        resolve({ 
          success: true, 
          chipType,
          suggestedBoard: suggestBoardFromChip(chipType),
          output 
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
function suggestBoardFromChip(chipType) {
  const chipLower = chipType.toLowerCase();
  
  if (chipLower.includes('esp32-c3')) {
    return 'esp32-c3-supermini'; // Most common C3 board
  } else if (chipLower.includes('esp32-s3')) {
    return 'esp32-s3';
  } else if (chipLower.includes('esp32-s2')) {
    return 'esp32-s2';
  } else if (chipLower.includes('esp32')) {
    return 'esp32dev'; // Original ESP32
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
    // Fetch all releases (including pre-releases), then get the first one
    const response = await axios.get(
      'https://api.github.com/repos/RaceFPV/StarForgeOS/releases',
      {
        headers: {
          'Accept': 'application/vnd.github.v3+json',
          'User-Agent': 'StarForge-Flasher'
        }
      }
    );
    
    if (!response.data || response.data.length === 0) {
      throw new Error('No releases found');
    }
    
    // Get the first release (most recent, including pre-releases)
    const latestRelease = response.data[0];
    
    return {
      tag: latestRelease.tag_name,
      name: latestRelease.name,
      assets: latestRelease.assets.map(asset => ({
        name: asset.name,
        url: asset.browser_download_url,
        size: asset.size
      }))
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
    
    const zipPath = path.join(cacheDir, `${boardType}.zip`);
    const extractPath = path.join(cacheDir, boardType);
    
    // Download
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
    
    // Extract
    await extractZip(zipPath, { dir: extractPath });
    
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

// Generate SPIFFS partition with custom config
// Uses the same approach as PlatformIO: copy files from data/ directory and add config.json
async function generateCustomSPIFFS(customConfig, firmwarePath = null) {
  const tempDir = app.getPath('temp');
  const configJsonPath = path.join(tempDir, 'sfos_custom_config.json');
  const spiffsImagePath = path.join(tempDir, 'custom_spiffs.bin');
  
  // Ensure default_mode is always present and valid
  if (!customConfig.default_mode) {
    customConfig.default_mode = 'standalone';  // Safe default
  }
  
  // Normalize default_mode value
  const defaultMode = String(customConfig.default_mode).toLowerCase().trim();
  if (defaultMode === 'standalone' || defaultMode === 'rotorhazard') {
    customConfig.default_mode = defaultMode;
  } else {
    console.warn(`Invalid default_mode value: ${customConfig.default_mode}, defaulting to 'standalone'`);
    customConfig.default_mode = 'standalone';
  }
  
  // Write config.json with proper formatting
  const configJson = JSON.stringify(customConfig, null, 2);
  await fs.writeFile(configJsonPath, configJson);
  
  // Log what we're writing (for debugging)
  console.log('Writing config.json:', configJson);
  
  // Run Python script to generate SPIFFS
  const scriptPath = path.join(__dirname, 'resources', 'scripts', 'generate_spiffs.py');
  
  // Determine resources path for bundled binaries
  let resourcesBinPath;
  if (app.isPackaged) {
    resourcesBinPath = path.join(process.resourcesPath, 'resources', 'bin');
  } else {
    resourcesBinPath = path.join(__dirname, 'resources', 'bin');
  }
  
  return new Promise((resolve, reject) => {
    // Pass resources path via environment variable so Python script can find bundled mkspiffs
    const env = { ...process.env, ELECTRON_RESOURCES_PATH: resourcesBinPath };
    // Pass firmware path as optional third argument to find data/ directory
    const pythonArgs = [scriptPath, configJsonPath, spiffsImagePath];
    if (firmwarePath) {
      pythonArgs.push(firmwarePath);
    }
    const python = spawn('python3', pythonArgs, { env });
    
    let output = '';
    
    python.stdout.on('data', (data) => {
      output += data.toString();
      console.log(data.toString());
    });
    
    python.stderr.on('data', (data) => {
      output += data.toString();
      console.error(data.toString());
    });
    
    python.on('close', (code) => {
      if (code === 0) {
        resolve(spiffsImagePath);
      } else {
        reject(new Error(`SPIFFS generation failed: ${output}`));
      }
    });
    
    python.on('error', (err) => {
      // Try 'python' instead of 'python3'
      if (err.code === 'ENOENT') {
        const env = { ...process.env, ELECTRON_RESOURCES_PATH: resourcesBinPath };
        const pythonArgs = [scriptPath, configJsonPath, spiffsImagePath];
        if (firmwarePath) {
          pythonArgs.push(firmwarePath);
        }
        const pythonAlt = spawn('python', pythonArgs, { env });
        
        pythonAlt.stdout.on('data', (data) => console.log(data.toString()));
        pythonAlt.stderr.on('data', (data) => console.error(data.toString()));
        
        pythonAlt.on('close', (code) => {
          if (code === 0) {
            resolve(spiffsImagePath);
          } else {
            reject(new Error('SPIFFS generation failed'));
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

// Helper function to find firmware files in PlatformIO build directories
async function findFirmwareFiles(firmwarePath, boardType, progressCallback = null) {
  const fsSync = require('fs');
  const files = {
    bootloader: null,
    partitions: null,
    firmware: null,
    spiffs: null
  };
  
  // First, check if files are directly in the firmwarePath (for downloaded releases)
  const directPaths = {
    bootloader: path.join(firmwarePath, 'bootloader.bin'),
    partitions: path.join(firmwarePath, 'partitions.bin'),
    firmware: path.join(firmwarePath, 'firmware.bin'),
    spiffs: path.join(firmwarePath, 'spiffs.bin')
  };
  
  let foundAny = false;
  Object.keys(directPaths).forEach(key => {
    if (fsSync.existsSync(directPaths[key])) {
      files[key] = directPaths[key];
      foundAny = true;
    }
  });
  
  // If files found directly, return them
  if (foundAny) {
    return files;
  }
  
  // Otherwise, search for PlatformIO build directories
  const pioBuildPath = path.join(firmwarePath, '.pio', 'build');
  if (fsSync.existsSync(pioBuildPath)) {
    try {
      const buildDirs = await fs.readdir(pioBuildPath);
      
      // Look for build directory matching board type or any build directory
      let searchDirs = buildDirs.filter(dir => {
        const dirPath = path.join(pioBuildPath, dir);
        return fsSync.statSync(dirPath).isDirectory();
      });
      
      // Prefer directory matching board type
      const matchingDir = searchDirs.find(dir => dir.includes(boardType.replace('esp32-', '')));
      if (matchingDir) {
        searchDirs = [matchingDir, ...searchDirs.filter(d => d !== matchingDir)];
      }
      
      // Search in each build directory
      for (const buildDir of searchDirs) {
        const buildDirPath = path.join(pioBuildPath, buildDir);
        const buildFiles = {
          bootloader: path.join(buildDirPath, 'bootloader.bin'),
          partitions: path.join(buildDirPath, 'partitions.bin'),
          firmware: path.join(buildDirPath, 'firmware.bin'),
          spiffs: path.join(buildDirPath, 'spiffs.bin')
        };
        
        let foundInDir = false;
        Object.keys(buildFiles).forEach(key => {
          if (fsSync.existsSync(buildFiles[key])) {
            files[key] = buildFiles[key];
            foundInDir = true;
          }
        });
        
        if (foundInDir) {
          if (progressCallback) {
            progressCallback(`✓ Found firmware files in: .pio/build/${buildDir}/\n`);
          }
          break;
        }
      }
    } catch (error) {
      // Ignore errors, will fall through to error handling
    }
  }
  
  return files;
}

// Flash firmware using esptool.py
ipcMain.handle('flash-firmware', async (event, options) => {
  const { port, boardType, firmwarePath, baudRate = 921600, customConfig = null } = options;
  const config = BOARD_CONFIGS[boardType];
  
  if (!config) {
    throw new Error('Invalid board type');
  }
  
  return new Promise(async (resolve, reject) => {
    let esptoolCmd;
    try {
      esptoolCmd = findEsptool();
    } catch (error) {
      reject(error);
      return;
    }
    
    // Find firmware files (check direct path and PlatformIO build directories)
    event.sender.send('flash-progress', `\n=== Searching for firmware files ===\n`);
    event.sender.send('flash-progress', `Looking in: ${firmwarePath}\n`);
    
    const files = await findFirmwareFiles(firmwarePath, boardType, (msg) => {
      event.sender.send('flash-progress', msg);
    });
    
    // Build esptool.py command
    const args = [
      '--chip', config.chip,
      '--port', port,
      '--baud', baudRate.toString(),
      '--before', 'default_reset',
      '--after', 'hard_reset',
      'write_flash'
    ];
    
    // Add flash addresses and files
    let fileCount = 0;
    Object.keys(files).forEach(key => {
      if (files[key] && require('fs').existsSync(files[key])) {
        // Always include spiffs.bin - we'll overwrite it with custom SPIFFS if needed
        args.push(config.flashAddresses[key], files[key]);
        fileCount++;
        event.sender.send('flash-progress', `✓ Found ${key}.bin\n`);
      }
    });
    
    // Validate that we found at least some firmware files
    if (fileCount === 0) {
      const errorMsg = `No firmware files found!\n\n` +
        `Expected files:\n` +
        `  - firmware.bin (required)\n` +
        `  - bootloader.bin\n` +
        `  - partitions.bin\n` +
        `  - spiffs.bin\n\n` +
        `Searched in:\n` +
        `  - ${firmwarePath}\n` +
        `  - ${path.join(firmwarePath, '.pio', 'build', '*')}\n\n` +
        `For PlatformIO builds, select the repo root folder.\n` +
        `For releases, select the extracted firmware folder.`;
      event.sender.send('flash-progress', `\n✗ ${errorMsg}\n`);
      reject(new Error(errorMsg));
      return;
    }
    
    if (!files.firmware) {
      const errorMsg = 'firmware.bin is required but not found!';
      event.sender.send('flash-progress', `\n✗ ${errorMsg}\n`);
      reject(new Error(errorMsg));
      return;
    }
    
    // Generate and add custom SPIFFS with default_mode (only if customConfig provided)
    // If no customConfig, use the default spiffs.bin from PlatformIO (which works!)
    if (customConfig) {
      try {
        event.sender.send('flash-progress', '\n=== Generating config SPIFFS with default mode ===\n');
        // Pass firmware path to find data/ directory (same as PlatformIO uploadfs)
        const customSPIFFSPath = await generateCustomSPIFFS(customConfig, firmwarePath);
        event.sender.send('flash-progress', `✓ Config SPIFFS generated: ${customSPIFFSPath}\n`);
        
        // Replace the default spiffs.bin with our custom one (same address, so it overwrites)
        // Find and replace the spiffs entry in args
        const spiffsIndex = args.indexOf(config.flashAddresses.spiffs);
        if (spiffsIndex !== -1 && spiffsIndex + 1 < args.length) {
          args[spiffsIndex + 1] = customSPIFFSPath; // Replace the file path
          event.sender.send('flash-progress', `✓ Custom SPIFFS will replace default at ${config.flashAddresses.spiffs}\n`);
        } else {
          // If spiffs wasn't found, add it
          args.push(config.flashAddresses.spiffs, customSPIFFSPath);
          event.sender.send('flash-progress', `✓ Custom SPIFFS will be flashed at ${config.flashAddresses.spiffs}\n`);
        }
      } catch (error) {
        event.sender.send('flash-progress', `\n⚠ Config SPIFFS generation failed: ${error.message}\n`);
        if (error.message.includes('Permission denied') || error.message.includes('PermissionError')) {
          event.sender.send('flash-progress', '\n⚠ Permission error with mkspiffs!\n');
          event.sender.send('flash-progress', 'Try running: chmod +x ~/.platformio/packages/tool-mkspiffs/mkspiffs\n');
          event.sender.send('flash-progress', 'Or reinstall PlatformIO to fix permissions\n');
        }
        event.sender.send('flash-progress', '\n⚠ Continuing with standard firmware (default mode will NOT be set)\n');
        event.sender.send('flash-progress', '⚠ The device will use default mode from config.h (RotorHazard)\n');
      }
    } else {
      event.sender.send('flash-progress', '⚠ Warning: No config provided - default mode will not be set!\n');
    }
    
    // Spawn esptool
    const esptool = spawn(esptoolCmd, args);
    
    let output = '';
    
    esptool.stdout.on('data', (data) => {
      const text = data.toString();
      output += text;
      event.sender.send('flash-progress', text);
      
      // Parse progress from esptool output
      const progressMatch = text.match(/Writing at 0x[0-9a-f]+\.\.\. \((\d+) %\)/);
      if (progressMatch) {
        event.sender.send('flash-percent', parseInt(progressMatch[1]));
      }
    });
    
    esptool.stderr.on('data', (data) => {
      const text = data.toString();
      output += text;
      event.sender.send('flash-progress', text);
    });
    
    esptool.on('close', (code) => {
      if (code === 0) {
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

// Erase flash
ipcMain.handle('erase-flash', async (event, options) => {
  const { port, boardType } = options;
  const config = BOARD_CONFIGS[boardType];
  
  return new Promise((resolve, reject) => {
    let esptoolCmd;
    try {
      esptoolCmd = findEsptool();
    } catch (error) {
      reject(error);
      return;
    }
    
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

