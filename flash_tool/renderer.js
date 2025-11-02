// Global state
let boardConfigs = {};
let selectedBoard = null;
let selectedPort = null;
let firmwareSource = 'github';
let localFirmwarePath = null;

// Initialize the app
async function init() {
  // Load board configurations
  boardConfigs = await window.flasher.getBoardConfigs();
  populateBoardSelect();
  
  // Load serial ports
  await refreshPorts();
  
  // Setup event listeners
  document.getElementById('board-select').addEventListener('change', (e) => {
    selectedBoard = e.target.value;
  });
  
  document.getElementById('port-select').addEventListener('change', (e) => {
    selectedPort = e.target.value;
    document.getElementById('detect-btn').disabled = !selectedPort;
  });
  
  document.querySelectorAll('input[name="source"]').forEach(radio => {
    radio.addEventListener('change', (e) => {
      firmwareSource = e.target.value;
      document.getElementById('local-folder-group').style.display = 
        firmwareSource === 'local' ? 'block' : 'none';
    });
  });
  
  // Setup progress listeners
  window.flasher.onFlashProgress((text) => {
    appendToConsole(text);
  });
  
  window.flasher.onFlashPercent((percent) => {
    updateProgress(percent);
  });
  
  window.flasher.onDownloadProgress((percent) => {
    updateProgress(percent);
    document.getElementById('progress-bar').textContent = `Downloading ${percent}%`;
  });
  
  window.flasher.onEraseProgress((text) => {
    appendToConsole(text);
  });
}

// Populate board select dropdown
function populateBoardSelect() {
  const select = document.getElementById('board-select');
  
  Object.keys(boardConfigs).forEach(key => {
    const option = document.createElement('option');
    option.value = key;
    option.textContent = boardConfigs[key].name;
    select.appendChild(option);
  });
}

// Refresh serial ports
async function refreshPorts() {
  const select = document.getElementById('port-select');
  const detectBtn = document.getElementById('detect-btn');
  const helpText = document.getElementById('port-help');
  
  select.innerHTML = '<option value="">Select port...</option>';
  detectBtn.disabled = true;
  
  try {
    const ports = await window.flasher.getSerialPorts();
    
    if (ports.length === 0) {
      const option = document.createElement('option');
      option.value = '';
      option.textContent = 'No ports found - connect your board';
      option.disabled = true;
      select.appendChild(option);
      helpText.textContent = '⚠️ No devices found. Connect your ESP32 board via USB.';
      return;
    }
    
    // Add all ports
    let esp32PortFound = null;
    ports.forEach(port => {
      const option = document.createElement('option');
      option.value = port.path;
      
      // Mark likely ESP32 devices
      const label = port.isESP32 ? '🔌 ' : '';
      option.textContent = `${label}${port.path} ${port.manufacturer ? `(${port.manufacturer})` : ''}`;
      select.appendChild(option);
      
      // Remember first ESP32-like port
      if (port.isESP32 && !esp32PortFound) {
        esp32PortFound = port.path;
      }
    });
    
    // Auto-select if only one port or ESP32 device found
    if (ports.length === 1) {
      select.value = ports[0].path;
      selectedPort = ports[0].path;
      detectBtn.disabled = false;
      helpText.textContent = '✅ Port auto-selected. Click "Detect Board" to identify chip type.';
      
      // Auto-detect board type
      setTimeout(() => detectBoard(), 500);
    } else if (esp32PortFound) {
      select.value = esp32PortFound;
      selectedPort = esp32PortFound;
      detectBtn.disabled = false;
      helpText.textContent = '✅ ESP32 device detected! Click "Detect Board" to identify chip type.';
    } else {
      helpText.textContent = 'Select your board\'s serial port from the list above';
    }
  } catch (error) {
    showStatus('Error loading ports: ' + error.message, 'error');
  }
}

// Select local firmware folder
async function selectLocalFolder() {
  try {
    const folder = await window.flasher.selectFirmwareFolder();
    if (folder) {
      localFirmwarePath = folder;
      document.getElementById('folder-path').value = folder;
    }
  } catch (error) {
    showStatus('Error selecting folder: ' + error.message, 'error');
  }
}

// Detect board type
async function detectBoard() {
  if (!selectedPort) {
    showStatus('Please select a port first', 'error');
    return;
  }
  
  const detectBtn = document.getElementById('detect-btn');
  const boardSelect = document.getElementById('board-select');
  const helpText = document.getElementById('port-help');
  
  detectBtn.disabled = true;
  detectBtn.textContent = '⏳ Detecting...';
  helpText.textContent = 'Detecting chip type... Do not disconnect.';
  
  try {
    const result = await window.flasher.detectBoard(selectedPort);
    
    if (result.success && result.suggestedBoard) {
      boardSelect.value = result.suggestedBoard;
      selectedBoard = result.suggestedBoard;
      showStatus(`✅ Detected: ${result.chipType} - Board set to ${boardConfigs[result.suggestedBoard].name}`, 'success');
      helpText.textContent = `✅ Auto-detected: ${result.chipType}`;
    } else {
      showStatus(`⚠️ Detected chip: ${result.chipType}, but couldn't match to a board. Please select manually.`, 'info');
      helpText.textContent = `Detected: ${result.chipType} - Select board type manually`;
    }
  } catch (error) {
    showStatus('⚠️ Auto-detection failed: ' + error.message, 'info');
    helpText.textContent = 'Auto-detection failed. Please select board type manually.';
  } finally {
    detectBtn.disabled = false;
    detectBtn.textContent = '🔍 Detect Board';
  }
}

// Start flashing process
async function startFlashing() {
  // Validation
  if (!selectedBoard) {
    showStatus('Please select a board type', 'error');
    return;
  }
  
  if (!selectedPort) {
    showStatus('Please select a serial port', 'error');
    return;
  }
  
  if (firmwareSource === 'local' && !localFirmwarePath) {
    showStatus('Please select a firmware folder', 'error');
    return;
  }
  
  // Disable buttons
  setButtonsEnabled(false);
  
  // Show progress section
  document.getElementById('progress-section').classList.add('active');
  clearConsole();
  updateProgress(0);
  showStatus('Starting flash process...', 'info');
  
  try {
    let firmwarePath;
    
    if (firmwareSource === 'github') {
      // Download from GitHub
      showStatus('Fetching latest release...', 'info');
      const release = await window.flasher.fetchGitHubReleases();
      
      // Find the asset for this board
      const asset = release.assets.find(a => a.name.includes(selectedBoard));
      
      if (!asset) {
        throw new Error(`No firmware found for ${selectedBoard} in latest release`);
      }
      
      showStatus(`Downloading ${release.name}...`, 'info');
      const download = await window.flasher.downloadFirmware(asset.url, selectedBoard);
      firmwarePath = download.path;
    } else {
      firmwarePath = localFirmwarePath;
    }
    
    // Flash the firmware
    showStatus('Flashing firmware... Do not disconnect!', 'info');
    appendToConsole('\n=== Starting flash process ===\n');
    
    const result = await window.flasher.flashFirmware({
      port: selectedPort,
      boardType: selectedBoard,
      firmwarePath: firmwarePath,
      baudRate: 921600
    });
    
    if (result.success) {
      updateProgress(100);
      showStatus('✅ Flash completed successfully! You can disconnect your board.', 'success');
      appendToConsole('\n=== Flash complete! ===\n');
    }
  } catch (error) {
    showStatus('❌ Flash failed: ' + error.message, 'error');
    appendToConsole('\n=== ERROR ===\n' + error.message + '\n');
  } finally {
    setButtonsEnabled(true);
  }
}

// Erase flash
async function eraseFlash() {
  if (!selectedBoard || !selectedPort) {
    showStatus('Please select board type and port', 'error');
    return;
  }
  
  if (!confirm('This will erase all data on the device. Continue?')) {
    return;
  }
  
  setButtonsEnabled(false);
  document.getElementById('progress-section').classList.add('active');
  clearConsole();
  showStatus('Erasing flash...', 'info');
  
  try {
    appendToConsole('=== Erasing flash ===\n');
    const result = await window.flasher.eraseFlash({
      port: selectedPort,
      boardType: selectedBoard
    });
    
    if (result.success) {
      showStatus('✅ Flash erased successfully', 'success');
      appendToConsole('\n=== Erase complete! ===\n');
    }
  } catch (error) {
    showStatus('❌ Erase failed: ' + error.message, 'error');
    appendToConsole('\n=== ERROR ===\n' + error.message + '\n');
  } finally {
    setButtonsEnabled(true);
  }
}

// UI Helper Functions
function updateProgress(percent) {
  const bar = document.getElementById('progress-bar');
  bar.style.width = percent + '%';
  bar.textContent = percent + '%';
}

function appendToConsole(text) {
  const console = document.getElementById('console-output');
  console.textContent += text;
  console.scrollTop = console.scrollHeight;
}

function clearConsole() {
  document.getElementById('console-output').textContent = '';
}

function showStatus(message, type) {
  const statusEl = document.getElementById('status-message');
  statusEl.textContent = message;
  statusEl.className = `status-message active ${type}`;
}

function setButtonsEnabled(enabled) {
  document.getElementById('flash-btn').disabled = !enabled;
  document.getElementById('erase-btn').disabled = !enabled;
  document.getElementById('board-select').disabled = !enabled;
  document.getElementById('port-select').disabled = !enabled;
}

// Initialize when DOM is ready
document.addEventListener('DOMContentLoaded', init);

