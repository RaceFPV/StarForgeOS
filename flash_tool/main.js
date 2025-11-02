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

// Flash firmware using esptool.py
ipcMain.handle('flash-firmware', async (event, options) => {
  const { port, boardType, firmwarePath, baudRate = 921600 } = options;
  const config = BOARD_CONFIGS[boardType];
  
  if (!config) {
    throw new Error('Invalid board type');
  }
  
  return new Promise((resolve, reject) => {
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
    
    // Add flash addresses and files
    const files = {
      bootloader: path.join(firmwarePath, 'bootloader.bin'),
      partitions: path.join(firmwarePath, 'partitions.bin'),
      firmware: path.join(firmwarePath, 'firmware.bin'),
      spiffs: path.join(firmwarePath, 'spiffs.bin')
    };
    
    // Check which files exist and add them
    Object.keys(files).forEach(key => {
      if (require('fs').existsSync(files[key])) {
        args.push(config.flashAddresses[key], files[key]);
      }
    });
    
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

