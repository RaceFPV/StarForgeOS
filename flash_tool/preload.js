const { contextBridge, ipcRenderer } = require('electron');

// Expose protected methods that allow the renderer process to use
// the ipcRenderer without exposing the entire object
contextBridge.exposeInMainWorld('flasher', {
  // Serial port operations
  getSerialPorts: () => ipcRenderer.invoke('get-serial-ports'),
  detectBoard: (port) => ipcRenderer.invoke('detect-board', port),
  
  // Board configurations
  getBoardConfigs: () => ipcRenderer.invoke('get-board-configs'),
  
  // GitHub operations
  fetchGitHubReleases: () => ipcRenderer.invoke('fetch-github-releases'),
  downloadFirmware: (url, boardType) => ipcRenderer.invoke('download-firmware', url, boardType),
  
  // Flash operations
  flashFirmware: (options) => ipcRenderer.invoke('flash-firmware', options),
  eraseFlash: (options) => ipcRenderer.invoke('erase-flash', options),
  
  // File operations
  selectFirmwareFolder: () => ipcRenderer.invoke('select-firmware-folder'),
  
  // Event listeners
  onDownloadProgress: (callback) => {
    ipcRenderer.on('download-progress', (event, percent) => callback(percent));
  },
  onFlashProgress: (callback) => {
    ipcRenderer.on('flash-progress', (event, text) => callback(text));
  },
  onFlashPercent: (callback) => {
    ipcRenderer.on('flash-percent', (event, percent) => callback(percent));
  },
  onEraseProgress: (callback) => {
    ipcRenderer.on('erase-progress', (event, text) => callback(text));
  }
});

