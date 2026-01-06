// Global state
let boardConfigs = {};
let selectedBoard = null;
let selectedPort = null;
let firmwareSource = 'github';
let localFirmwarePath = null;
let customPinsEnabled = false;
let allReleases = [];
let selectedRelease = null; // Will be set to latest stable release on load
let latestReleaseTag = null; // The actual latest stable release tag from GitHub

// Console output buffering for real-time updates
let consoleBuffer = '';
let consoleUpdateScheduled = false;

// Serial monitor buffering
let serialMonitorBuffer = '';
let serialMonitorUpdateScheduled = false;
let serialMonitoring = false;

// Pin presets for different boards
const pinPresets = {
  'esp32c3-default': {
    rssi_input: 3,
    rx5808_data: 6,
    rx5808_clk: 4,
    rx5808_sel: 7,
    mode_switch: 1,
    power_button: 0,
    battery_adc: 0,
    audio_dac: 0
  },
  'esp32c6-default': {
    rssi_input: 0,
    rx5808_data: 6,
    rx5808_clk: 4,
    rx5808_sel: 7,
    mode_switch: 1,
    power_button: 0,
    battery_adc: 0,
    audio_dac: 0
  },
  'esp32-default': {
    rssi_input: 34,
    rx5808_data: 23,
    rx5808_clk: 18,
    rx5808_sel: 5,
    mode_switch: 33,
    power_button: 0,
    battery_adc: 0,
    audio_dac: 26
  }
};

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
    // Remove highlight when user manually selects
    e.target.style.border = '';
  });
  
  document.getElementById('port-select').addEventListener('change', (e) => {
    selectedPort = e.target.value;
    document.getElementById('detect-btn').disabled = !selectedPort;
  });
  
  document.querySelectorAll('input[name="source"]').forEach(radio => {
    radio.addEventListener('change', (e) => {
      firmwareSource = e.target.value;
      const localFolderGroup = document.getElementById('local-folder-group');
      const githubReleaseGroup = document.getElementById('github-release-group');
      
      localFolderGroup.style.display = firmwareSource === 'local' ? 'block' : 'none';
      githubReleaseGroup.style.display = firmwareSource === 'github' ? 'block' : 'none';
      
      // Scroll to local folder section when it's shown
      if (firmwareSource === 'local') {
        setTimeout(() => {
          localFolderGroup.scrollIntoView({ behavior: 'smooth', block: 'nearest' });
        }, 100);
      }
    });
  });
  
  // Load GitHub releases on startup
  await loadReleases();
  
  // Setup progress listeners
  window.flasher.onFlashProgress((text) => {
    appendToConsole(text);
  });
  
  // Removed flash-percent listener - progress bar was inaccurate
  // Users can see progress in the console output instead
  
  window.flasher.onDownloadProgress((percent) => {
    // Only show progress for downloads (GitHub releases), not for local builds
    updateProgress(percent);
    document.getElementById('progress-bar').textContent = `Downloading ${percent}%`;
  });
  
  window.flasher.onEraseProgress((text) => {
    appendToConsole(text);
  });

  // Serial monitor listeners
  window.flasher.onSerialMonitorData((text) => {
    appendToSerialMonitor(text);
  });

  window.flasher.onSerialMonitorStatus((status) => {
    const btn = document.getElementById('serial-monitor-btn');
    const help = document.getElementById('serial-monitor-help');
    if (!btn || !help) return;

    if (status.type === 'open') {
      serialMonitoring = true;
      btn.textContent = '🛑 Stop Serial Monitor';
      help.textContent = `Connected to ${status.port} @ ${status.baudRate} baud.`;
    } else if (status.type === 'close') {
      serialMonitoring = false;
      btn.textContent = '🖥️ Show Serial Output';
      help.textContent = 'Uses the selected serial port at 115200 baud.';
    } else if (status.type === 'error') {
      serialMonitoring = false;
      btn.textContent = '🖥️ Show Serial Output';
      help.textContent = `Error: ${status.message}`;
      showStatus('Serial monitor error: ' + status.message, 'error');
    }
  });
}

// Load and populate GitHub releases
async function loadReleases() {
  try {
    const releaseSelect = document.getElementById('release-select');
    const releaseHelp = document.getElementById('release-help');
    
    releaseSelect.innerHTML = '<option value="">Loading releases...</option>';
    releaseSelect.disabled = true;
    releaseHelp.textContent = 'Loading available releases...';
    
    const releasesData = await window.flasher.fetchGitHubReleases();
    allReleases = releasesData.releases;
    latestReleaseTag = releasesData.latestTag;
    
    if (allReleases.length === 0) {
      releaseSelect.innerHTML = '<option value="">No releases found</option>';
      releaseHelp.textContent = '⚠️ No releases available';
      return;
    }
    
    // Populate dropdown
    releaseSelect.innerHTML = '';
    let latestIndex = 0; // Default to first if we can't find latest
    
    allReleases.forEach((release, index) => {
      const option = document.createElement('option');
      option.value = index;
      let label = release.isPrerelease 
        ? `${release.name || release.tag} (Pre-release)`
        : release.name || release.tag;
      
      // Mark the latest stable release
      if (latestReleaseTag && release.tag === latestReleaseTag) {
        label = `${label} (Latest)`;
        latestIndex = index;
      }
      
      option.textContent = label;
      releaseSelect.appendChild(option);
    });
    
    // Default to latest stable release (or first if latest not found)
    releaseSelect.value = latestIndex.toString();
    selectedRelease = allReleases[latestIndex];
    releaseSelect.disabled = false;
    const releaseName = selectedRelease.name || selectedRelease.tag;
    const isLatest = latestReleaseTag && selectedRelease.tag === latestReleaseTag;
    releaseHelp.textContent = `✅ ${allReleases.length} release${allReleases.length > 1 ? 's' : ''} available. ${isLatest ? 'Latest stable' : 'Selected'}: ${releaseName}${selectedRelease.isPrerelease ? ' (Pre-release)' : ''}`;
    
    // Trigger change event to ensure state is consistent
    releaseSelect.dispatchEvent(new Event('change'));
    
    // Add change listener (only add once)
    if (!releaseSelect.hasAttribute('data-listener-added')) {
      releaseSelect.setAttribute('data-listener-added', 'true');
      releaseSelect.addEventListener('change', (e) => {
        const index = parseInt(e.target.value);
        if (index >= 0 && index < allReleases.length) {
          selectedRelease = allReleases[index];
          const releaseName = selectedRelease.name || selectedRelease.tag;
          const isLatest = latestReleaseTag && selectedRelease.tag === latestReleaseTag;
          releaseHelp.textContent = `${isLatest ? 'Latest stable' : 'Selected'}: ${releaseName}${selectedRelease.isPrerelease ? ' (Pre-release)' : ''}`;
        }
      });
    }
  } catch (error) {
    const releaseSelect = document.getElementById('release-select');
    const releaseHelp = document.getElementById('release-help');
    releaseSelect.innerHTML = '<option value="">Error loading releases</option>';
    releaseHelp.textContent = `❌ Error: ${error.message}`;
    showStatus('Failed to load releases: ' + error.message, 'error');
  }
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
    } else if (result.requiresManualSelection) {
      // ESP32 detected but flash size unknown - require manual selection
      showStatus(`⚠️ Detected: ${result.chipType} - Please manually select your board type (2MB or 4MB flash)`, 'warning');
      helpText.textContent = `⚠️ Detected: ${result.chipType} - Select board type: ESP32 Dev Module (2MB) or ESP32 Dev Module (4MB)`;
      // Highlight the board select dropdown
      boardSelect.style.border = '2px solid #ff9800';
      boardSelect.focus();
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

  // Ensure serial monitor is stopped before flashing
  try {
    await window.flasher.stopSerialMonitor();
  } catch (e) {
    // Ignore monitor stop errors
  }
  
  // Disable buttons
  setButtonsEnabled(false);
  
  // Show progress section
  const progressSection = document.getElementById('progress-section');
  progressSection.classList.add('active');
  clearConsole();
  // Hide progress bar for local builds (only show for downloads)
  const progressBar = document.getElementById('progress-bar');
  progressBar.style.display = 'none';
  showStatus('Starting flash process...', 'info');
  
  // Scroll to progress section so user can see the debug output
  setTimeout(() => {
    progressSection.scrollIntoView({ behavior: 'smooth', block: 'nearest' });
  }, 100);
  
  try {
    let firmwarePath;
    
    if (firmwareSource === 'github') {
      // Download from GitHub
      // If no release is selected, try to use the latest (first in array)
      if (!selectedRelease) {
        if (allReleases.length > 0) {
          selectedRelease = allReleases[0];
          const releaseSelect = document.getElementById('release-select');
          if (releaseSelect) {
            releaseSelect.value = '0';
          }
        } else {
          throw new Error('No releases available. Please check your internet connection and try again.');
        }
      }
      
      showStatus(`Fetching ${selectedRelease.name || selectedRelease.tag}...`, 'info');
      // Show progress bar for downloads
      const progressBar = document.getElementById('progress-bar');
      progressBar.style.display = 'block';
      updateProgress(0);
      
      // Find the asset for this board
      // Match must be exact to prevent "esp32-s3" from matching "esp32-s3-touch"
      // Check for exact pattern: "StarForge-{board}.zip" or ends with "-{board}.zip"
      const asset = selectedRelease.assets.find(a => {
        const expectedName = `StarForge-${selectedBoard}.zip`;
        // Exact match
        if (a.name === expectedName) return true;
        // Ends with the board name followed by .zip (prevents substring matches)
        if (a.name.endsWith(`-${selectedBoard}.zip`)) return true;
        return false;
      });
      
      if (!asset) {
        throw new Error(`No firmware found for ${selectedBoard} in ${selectedRelease.name || selectedRelease.tag}`);
      }
      
      showStatus(`Downloading ${selectedRelease.name || selectedRelease.tag}...`, 'info');
      const download = await window.flasher.downloadFirmware(asset.url, selectedBoard);
      firmwarePath = download.path;
    } else {
      firmwarePath = localFirmwarePath;
    }
    
    // Flash the firmware
    showStatus('Flashing firmware... Do not disconnect!', 'info');
    appendToConsole('\n=== Starting flash process ===\n');
    
    // Get custom pin configuration (if enabled)
    const customConfig = getCustomPinConfig();
    
    const result = await window.flasher.flashFirmware({
      port: selectedPort,
      boardType: selectedBoard,
      firmwarePath: firmwarePath,
      baudRate: 921600,
      customConfig: customConfig
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

// Erase flash (selective - only app and SPIFFS, preserves NVS)
async function eraseFlash() {
  if (!selectedBoard || !selectedPort) {
    showStatus('Please select board type and port', 'error');
    return;
  }

  // Ensure serial monitor is stopped before erasing
  try {
    await window.flasher.stopSerialMonitor();
  } catch (e) {
    // Ignore monitor stop errors
  }
  
  setButtonsEnabled(false);
  const progressSection = document.getElementById('progress-section');
  progressSection.classList.add('active');
  clearConsole();
  showStatus('Erasing flash...', 'info');
  
  // Scroll to progress section so user can see the debug output
  setTimeout(() => {
    progressSection.scrollIntoView({ behavior: 'smooth', block: 'nearest' });
  }, 100);
  
  try {
    appendToConsole('=== Erasing flash ===\n');
    const result = await window.flasher.eraseFlash({
      port: selectedPort,
      boardType: selectedBoard,
      fullErase: false
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

// Erase all (full chip erase - everything including NVS)
async function eraseAllFlash() {
  if (!selectedBoard || !selectedPort) {
    showStatus('Please select board type and port', 'error');
    return;
  }
  
  // Warning dialog
  if (!confirm('⚠️ WARNING: Full Chip Erase\n\n' +
               'This will PERMANENTLY DELETE:\n' +
               '• All firmware\n' +
               '• All SPIFFS files\n' +
               '• WiFi calibration data\n' +
               '• MAC address settings\n' +
               '• All NVS data\n' +
               '• Factory calibration\n\n' +
               'This is IRREVERSIBLE and will require re-flashing everything.\n\n' +
               'Continue with full chip erase?')) {
    return;
  }

  // Ensure serial monitor is stopped before erasing
  try {
    await window.flasher.stopSerialMonitor();
  } catch (e) {
    // Ignore monitor stop errors
  }
  
  setButtonsEnabled(false);
  const progressSection = document.getElementById('progress-section');
  progressSection.classList.add('active');
  clearConsole();
  showStatus('Performing full chip erase...', 'info');
  
  // Scroll to progress section so user can see the debug output
  setTimeout(() => {
    progressSection.scrollIntoView({ behavior: 'smooth', block: 'nearest' });
  }, 100);
  
  try {
    appendToConsole('=== Full Chip Erase ===\n');
    appendToConsole('⚠️ This will erase ALL data on the device\n\n');
    const result = await window.flasher.eraseFlash({
      port: selectedPort,
      boardType: selectedBoard,
      fullErase: true
    });
    
    if (result.success) {
      showStatus('✅ Full chip erase complete', 'success');
      appendToConsole('\n=== Full Chip Erase Complete! ===\n');
      appendToConsole('⚠️ All data has been wiped. You can now flash fresh firmware.\n');
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
  // Buffer the text for batched updates
  consoleBuffer += text;
  
  // Schedule a DOM update if not already scheduled
  if (!consoleUpdateScheduled) {
    consoleUpdateScheduled = true;
    requestAnimationFrame(flushConsoleBuffer);
  }
}

function flushConsoleBuffer() {
  if (consoleBuffer.length === 0) {
    consoleUpdateScheduled = false;
    return;
  }
  
  const console = document.getElementById('console-output');
  const textToAppend = consoleBuffer;
  consoleBuffer = ''; // Clear buffer before DOM update
  consoleUpdateScheduled = false;
  
  // Use appendChild with text nodes for better performance
  // This avoids full re-render of existing content
  const textNode = document.createTextNode(textToAppend);
  console.appendChild(textNode);
  
  // Scroll to bottom (use scrollTop for immediate scroll, no animation)
  // Use setTimeout(0) to ensure scroll happens after DOM update
  setTimeout(() => {
    console.scrollTop = console.scrollHeight;
  }, 0);
  
  // If more data arrived during the update, schedule another flush
  if (consoleBuffer.length > 0) {
    consoleUpdateScheduled = true;
    requestAnimationFrame(flushConsoleBuffer);
  }
}

function clearConsole() {
  consoleBuffer = '';
  consoleUpdateScheduled = false;
  document.getElementById('console-output').textContent = '';
}

function appendToSerialMonitor(text) {
  serialMonitorBuffer += text;

  if (!serialMonitorUpdateScheduled) {
    serialMonitorUpdateScheduled = true;
    requestAnimationFrame(flushSerialMonitorBuffer);
  }
}

function flushSerialMonitorBuffer() {
  if (serialMonitorBuffer.length === 0) {
    serialMonitorUpdateScheduled = false;
    return;
  }

  const consoleEl = document.getElementById('serial-monitor-output');
  if (!consoleEl) {
    serialMonitorBuffer = '';
    serialMonitorUpdateScheduled = false;
    return;
  }

  const textToAppend = serialMonitorBuffer;
  serialMonitorBuffer = '';
  serialMonitorUpdateScheduled = false;

  const textNode = document.createTextNode(textToAppend);
  consoleEl.appendChild(textNode);

  setTimeout(() => {
    consoleEl.scrollTop = consoleEl.scrollHeight;
  }, 0);

  if (serialMonitorBuffer.length > 0) {
    serialMonitorUpdateScheduled = true;
    requestAnimationFrame(flushSerialMonitorBuffer);
  }
}

function clearSerialMonitor() {
  serialMonitorBuffer = '';
  serialMonitorUpdateScheduled = false;
  const consoleEl = document.getElementById('serial-monitor-output');
  if (consoleEl) {
    consoleEl.textContent = '';
  }
}

function showStatus(message, type) {
  const statusEl = document.getElementById('status-message');
  statusEl.textContent = message;
  statusEl.className = `status-message active ${type}`;
}

function setButtonsEnabled(enabled) {
  document.getElementById('flash-btn').disabled = !enabled;
  document.getElementById('erase-btn').disabled = !enabled;
  const eraseAllBtn = document.getElementById('erase-all-btn');
  if (eraseAllBtn) {
    eraseAllBtn.disabled = !enabled;
  }
  document.getElementById('board-select').disabled = !enabled;
  document.getElementById('port-select').disabled = !enabled;
}

// Serial monitor UI
async function toggleSerialMonitor() {
  if (!selectedPort) {
    showStatus('Please select a serial port before starting the serial monitor', 'error');
    return;
  }

  const btn = document.getElementById('serial-monitor-btn');
  const help = document.getElementById('serial-monitor-help');
  if (!btn || !help) return;

  if (!serialMonitoring) {
    clearSerialMonitor();
    btn.disabled = true;
    help.textContent = 'Connecting to serial port...';
    try {
      const baudSelect = document.getElementById('serial-baud');
      const baudRate = baudSelect ? parseInt(baudSelect.value, 10) || 115200 : 115200;
      await window.flasher.startSerialMonitor({
        port: selectedPort,
        baudRate
      });
      // Status handler will update UI
    } catch (error) {
      serialMonitoring = false;
      btn.textContent = '🖥️ Show Serial Output';
      help.textContent = 'Failed to open serial port.';
      showStatus('Failed to start serial monitor: ' + error.message, 'error');
    } finally {
      btn.disabled = false;
    }
  } else {
    btn.disabled = true;
    help.textContent = 'Stopping serial monitor...';
    try {
      await window.flasher.stopSerialMonitor();
      // Status handler will update UI
    } catch (error) {
      serialMonitoring = false;
      btn.textContent = '🖥️ Show Serial Output';
      help.textContent = 'Serial monitor stopped.';
    } finally {
      btn.disabled = false;
    }
  }
}

// Advanced Config UI Functions
function toggleAdvancedConfig() {
  const body = document.getElementById('advanced-config-body');
  const toggle = document.getElementById('advanced-toggle');
  
  if (body.style.display === 'none') {
    body.style.display = 'block';
    toggle.classList.add('open');
  } else {
    body.style.display = 'none';
    toggle.classList.remove('open');
  }
}

function toggleCustomPins() {
  const enabled = document.getElementById('enable-custom-pins').checked;
  const section = document.getElementById('custom-pins-section');
  customPinsEnabled = enabled;
  
  section.style.display = enabled ? 'block' : 'none';
}

function loadPinPreset() {
  const preset = document.getElementById('pin-preset').value;
  if (!preset || !pinPresets[preset]) return;
  
  const pins = pinPresets[preset];
  document.getElementById('pin-rssi').value = pins.rssi_input;
  document.getElementById('pin-data').value = pins.rx5808_data;
  document.getElementById('pin-clk').value = pins.rx5808_clk;
  document.getElementById('pin-sel').value = pins.rx5808_sel;
  document.getElementById('pin-mode').value = pins.mode_switch;
  document.getElementById('pin-power').value = pins.power_button || 0;
  document.getElementById('pin-battery').value = pins.battery_adc || 0;
  document.getElementById('pin-audio').value = pins.audio_dac || 0;
}

function getCustomPinConfig() {
  if (!customPinsEnabled) {
    return null;
  }
  
  return {
    custom_pins: {
      enabled: true,
      rssi_input: parseInt(document.getElementById('pin-rssi').value) || 3,
      rx5808_data: parseInt(document.getElementById('pin-data').value) || 6,
      rx5808_clk: parseInt(document.getElementById('pin-clk').value) || 4,
      rx5808_sel: parseInt(document.getElementById('pin-sel').value) || 7,
      mode_switch: parseInt(document.getElementById('pin-mode').value) || 1
    }
  };
}

// Initialize when DOM is ready
document.addEventListener('DOMContentLoaded', init);

