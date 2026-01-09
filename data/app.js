// Race Timer JavaScript
class RaceTimer {
    constructor() {
        this.socket = null;
        this.isConnected = false;
        this.raceActive = false;
        this.laps = [];
        this.currentStatus = {};
        this.audioEnabled = true;
        this.speechSynthesis = window.speechSynthesis;
        this.lastLapCount = null;  // Track last known lap count to avoid unnecessary /api/laps calls

        // RSSI Graph variables
        this.rssiHistory = [];
        this.maxHistory = 150; // 150 points for scrolling graph
        this.rssiCanvas = null;
        this.rssiCtx = null;

        // Race data recording for export
        this.raceData = {
            rssiSamples: [],       // Full RSSI history with timestamps
            laps: [],              // Lap data
            startTime: null,       // Race start timestamp
            endTime: null,         // Race end timestamp
            enterThreshold: 120,   // Enter RSSI threshold
            exitThreshold: 100,    // Exit RSSI threshold
            frequency: 5800        // VTX frequency
        };
        this.recordingActive = false;

        this.initializeUI();
        this.initializeCanvas();
        this.loadChannels();
        this.connectWebSocket();
    }
    
    initializeCanvas() {
        this.rssiCanvas = document.getElementById('rssiCanvas');
        if (this.rssiCanvas) {
            this.rssiCtx = this.rssiCanvas.getContext('2d');
            // Set canvas size to match display size
            const container = this.rssiCanvas.parentElement;
            this.rssiCanvas.width = container.clientWidth;
            this.rssiCanvas.height = container.clientHeight;
            
            console.log('Canvas initialized:', this.rssiCanvas.width, 'x', this.rssiCanvas.height);
            
            // Start drawing loop
            this.drawGraph();
        } else {
            console.error('RSSI Canvas not found');
        }
    }
    
    async loadChannels() {
        try {
            console.log('Loading channels from API...');
            const response = await fetch('/api/get_channels');
            if (!response.ok) {
                throw new Error(`HTTP error! status: ${response.status}`);
            }
            const data = await response.json();
            console.log('Channels loaded:', data);
            this.populateChannels(data);
        } catch (error) {
            console.error('Error loading channels:', error);
            // Set a fallback message
            const channelSelect = document.getElementById('channelSelect');
            if (channelSelect) {
                channelSelect.innerHTML = '<option value="">Error loading channels</option>';
            }
        }
    }
    
    populateChannels(data) {
        const bandSelect = document.getElementById('bandSelect');
        const channelSelect = document.getElementById('channelSelect');
        
        if (!bandSelect || !channelSelect) {
            console.error('Band or channel select elements not found');
            return;
        }
        
        if (!data.bands) {
            console.error('No bands data in response:', data);
            channelSelect.innerHTML = '<option value="">No channels available</option>';
            return;
        }
        
        // Store channel data
        this.channelData = data.bands;
        
        // Populate channels for selected band
        const updateChannelOptions = (bandName) => {
            const channels = this.channelData[bandName] || [];
            channelSelect.innerHTML = '';
            
            if (channels.length === 0) {
                channelSelect.innerHTML = '<option value="">No channels for this band</option>';
                return;
            }
            
            channels.forEach(ch => {
                const option = document.createElement('option');
                option.value = ch.frequency;
                option.textContent = `${ch.channel} (${ch.frequency} MHz)`;
                channelSelect.appendChild(option);
            });
            
            console.log(`Populated ${channels.length} channels for ${bandName}`);
        };
        
        // Initial population
        updateChannelOptions(bandSelect.value);
        
        // Store reference for global function
        this.updateChannelOptions = updateChannelOptions;
    }
    
    initializeUI() {
        // Initialize enter RSSI slider
        const enterRssiSlider = document.getElementById('enterRssiSlider');
        const enterRssiValue = document.getElementById('enterRssiValue');
        
        if (enterRssiSlider && enterRssiValue) {
            enterRssiSlider.addEventListener('input', (e) => {
                enterRssiValue.textContent = e.target.value;
                this.updateThresholdLines();
            });
        }
        
        // Initialize exit RSSI slider
        const exitRssiSlider = document.getElementById('exitRssiSlider');
        const exitRssiValue = document.getElementById('exitRssiValue');
        
        if (exitRssiSlider && exitRssiValue) {
            exitRssiSlider.addEventListener('input', (e) => {
                exitRssiValue.textContent = e.target.value;
                this.updateThresholdLines();
            });
        }
        
        // Initialize threshold line positions
        this.updateThresholdLines();
    }
    
    connectWebSocket() {
        // Use HTTP polling instead of WebSocket for standalone mode
        console.log('Starting HTTP polling mode...');
        this.setConnectionStatus(true);
        this.startPolling();
    }
    
    startPolling() {
        // Poll for updates every 250ms
        this.pollInterval = setInterval(() => {
            this.updateData();
        }, 250);
        console.log('Polling started');
    }
    
    stopPolling() {
        if (this.pollInterval) {
            clearInterval(this.pollInterval);
            this.pollInterval = null;
        }
    }
    
    async updateData() {
        try {
            // Get status (always fetch - it's small and changes frequently)
            const statusResponse = await fetch('/api/status');
            const status = await statusResponse.json();
            this.updateStatus(status);
            
            // Only fetch laps if lap count changed (optimization: laps array is large)
            const currentLapCount = status.lap_count || 0;
            if (this.lastLapCount === null || this.lastLapCount !== currentLapCount) {
                // Lap count changed or first load - fetch full lap data
                const lapsResponse = await fetch('/api/laps');
                const laps = await lapsResponse.json();
                this.updateLaps(laps);
                this.lastLapCount = currentLapCount;
            }
            // else: lap count unchanged, skip expensive /api/laps call
            
        } catch (error) {
            console.error('Error updating data:', error);
            this.setConnectionStatus(false);
        }
    }
    
    setConnectionStatus(connected) {
        this.isConnected = connected;
        const statusDot = document.getElementById('statusDot');
        const statusText = document.getElementById('statusText');
        
        console.log('Setting connection status:', connected, 'Elements:', {statusDot, statusText});
        
        if (statusDot && statusText) {
            if (connected) {
                statusDot.className = 'status-dot connected';
                statusText.textContent = 'Connected';
            } else {
                statusDot.className = 'status-dot connecting';
                statusText.textContent = 'Connecting...';
            }
        } else {
            console.error('Status elements not found in DOM');
        }
    }
    
    handleMessage(data) {
        switch (data.type) {
            case 'status':
                this.updateStatus(data);
                break;
            case 'lap':
                this.addLap(data);
                break;
            case 'race_state':
                this.updateRaceState(data);
                break;
            default:
                console.log('Unknown message type:', data.type);
        }
    }
    
    updateStatus(data) {
        this.currentStatus = data;

        // Store RSSI for graphing
        const rssiValue = data.rssi || data.current_rssi || 0;
        this.rssiHistory.push(rssiValue);

        // Keep history limited
        if (this.rssiHistory.length > this.maxHistory) {
            this.rssiHistory.shift();
        }

        // Record RSSI sample with timestamp for race data export
        if (this.recordingActive) {
            this.raceData.rssiSamples.push({
                timestamp: Date.now(),
                rssi: rssiValue
            });
        }

        // Update race data thresholds from current status
        if (data.enter_rssi !== undefined) {
            this.raceData.enterThreshold = data.enter_rssi;
        }
        if (data.exit_rssi !== undefined) {
            this.raceData.exitThreshold = data.exit_rssi;
        }
        if (data.frequency !== undefined) {
            this.raceData.frequency = data.frequency;
        }

        // Update race state indicator
        const isRacing = data.status === 'racing';
        const wasRacing = this.raceActive;
        this.raceActive = isRacing;
        this.updateRaceStatusIndicator(isRacing, wasRacing);
        
        // Update button state based on race status
        const startBtn = document.getElementById('startBtn');
        const stopBtn = document.getElementById('stopBtn');
        if (startBtn) {
            startBtn.disabled = isRacing;
            startBtn.textContent = isRacing ? 'Race started' : '▶ Start Race';
        }
        if (stopBtn) {
            stopBtn.disabled = !isRacing;
        }

        // Update lap count
        const lapCount = document.getElementById('lapCount');
        if (lapCount) {
            lapCount.textContent = data.lap_count || 0;
        }
        
        // Update RSSI displays (if they exist)
        const currentRssi = document.getElementById('currentRssi');
        if (currentRssi) {
            currentRssi.textContent = rssiValue;
        }
        
        const peakRssi = document.getElementById('peakRssi');
        if (peakRssi) {
            peakRssi.textContent = data.peak_rssi || 0;
        }
        
        // Update RSSI bar (if it exists)
        const rssiFill = document.getElementById('rssiFill');
        if (rssiFill) {
            const rssiPercent = Math.min(rssiValue / 255 * 100, 100);
            rssiFill.style.width = rssiPercent + '%';
        }
        
        // Show crossing state
        const statusItems = document.querySelectorAll('.status-item, .stat-card');
        if (data.crossing) {
            statusItems.forEach(item => item.classList.add('crossing'));
        } else {
            statusItems.forEach(item => item.classList.remove('crossing'));
        }
        
        // Update battery status if available
        if (data.battery) {
            const batteryCard = document.getElementById('batteryCard');
            const batteryPercentage = document.getElementById('batteryPercentage');
            const batteryVoltage = document.getElementById('batteryVoltage');
            
            if (batteryCard) {
                batteryCard.style.display = '';
                
                if (batteryPercentage) {
                    const percentage = data.battery.percentage || 0;
                    batteryPercentage.textContent = percentage + '%';
                    
                    // Color code battery level
                    if (percentage > 50) {
                        batteryPercentage.style.color = '#4ade80'; // Green
                    } else if (percentage > 20) {
                        batteryPercentage.style.color = '#fbbf24'; // Yellow
                    } else {
                        batteryPercentage.style.color = '#ef4444'; // Red
                    }
                }
                
                if (batteryVoltage) {
                    const voltage = data.battery.voltage || 0;
                    let voltageText = voltage.toFixed(2) + 'V';
                    
                    if (data.battery.charging) {
                        voltageText += ' ⚡';
                    }
                    batteryVoltage.textContent = voltageText;
                }
            }
        } else {
            // Hide battery card if not available
            const batteryCard = document.getElementById('batteryCard');
            if (batteryCard) {
                batteryCard.style.display = 'none';
            }
        }
    }
    
    updateRaceStatusIndicator(isRacing, wasRacing) {
        const raceStatusIndicator = document.getElementById('raceStatusIndicator');
        const raceStatusText = document.getElementById('raceStatusText');
        const raceStatus = document.getElementById('raceStatus');
        const container = document.body;
        
        if (!raceStatusIndicator || !raceStatusText || !raceStatus) {
            return;
        }
        
        if (isRacing) {
            // Race is active
            raceStatusIndicator.className = 'race-status-indicator racing';
            raceStatusText.textContent = 'Racing';
            raceStatus.className = 'race-status race-active';
            container.classList.add('race-active');
            container.classList.remove('race-stopped');
        } else {
            // Race is stopped/ready
            raceStatusIndicator.className = 'race-status-indicator stopped';
            raceStatusText.textContent = 'Ready';
            raceStatus.className = 'race-status race-stopped';
            container.classList.add('race-stopped');
            container.classList.remove('race-active');
            
            // If race just stopped (was racing before), add a flash effect
            if (wasRacing) {
                raceStatus.classList.add('race-stopped-flash');
                setTimeout(() => {
                    raceStatus.classList.remove('race-stopped-flash');
                }, 1000);
            }
        }
    }
    
    updateLaps(laps) {
        // Check for new laps by comparing with previous lap count
        const previousLapCount = this.laps.length;
        const currentLapCount = laps.length;

        if (currentLapCount > previousLapCount) {
            // New lap detected - announce it
            const newLap = laps[laps.length - 1];
            if (this.audioEnabled && newLap.lap_time_ms > 0) {
                this.announceLapTime(newLap);
            }
        }

        // Update laps array
        this.laps = laps;
        
        // Update lastLapCount to match current count
        this.lastLapCount = laps.length;

        // Update race data laps
        this.raceData.laps = laps;

        this.updateLapDisplay();
    }
    
    clearLaps() {
        // Clear laps and reset lap count tracking
        this.laps = [];
        this.lastLapCount = 0;
        this.raceData.laps = [];
        this.updateLapDisplay();
    }
    
    addLap(lapData) {
        this.laps.push(lapData);
        // Update lastLapCount when lap is added via WebSocket
        this.lastLapCount = this.laps.length;
        this.updateLapDisplay();
        this.updateLastLap(lapData);
        
        // Announce lap time if audio is enabled
        if (this.audioEnabled && lapData.lap_time_ms > 0) {
            this.announceLapTime(lapData);
        }
        
        // Flash animation for new lap
        setTimeout(() => {
            const lapItems = document.querySelectorAll('.lap-item');
            if (lapItems.length > 0) {
                lapItems[0].classList.add('new-lap');
                setTimeout(() => {
                    lapItems[0].classList.remove('new-lap');
                }, 1000);
            }
        }, 100);
    }
    
    updateLastLap(lapData) {
        const lastLapElement = document.getElementById('lastLap');
        if (lastLapElement && lapData.lap_time_ms > 0) {
            lastLapElement.textContent = this.formatTime(lapData.lap_time_ms);
        }
    }
    
    updateLapDisplay() {
        const lapList = document.getElementById('laps');
        
        if (!lapList) return;
        
        if (this.laps.length === 0) {
            lapList.innerHTML = '<div class="no-laps">No laps recorded yet</div>';
            return;
        }
        
        // Find best lap
        const validLaps = this.laps.filter(lap => lap.lap_time_ms > 0);
        const bestLap = validLaps.length > 0 ? 
            Math.min(...validLaps.map(lap => lap.lap_time_ms)) : null;
        
        lapList.innerHTML = this.laps.slice().reverse().map((lap, index) => {
            const lapNumber = this.laps.length - index;
            const isBest = lap.lap_time_ms === bestLap && lap.lap_time_ms > 0;
            const lapTime = lap.lap_time_ms > 0 ? this.formatTime(lap.lap_time_ms) : '--:--';
            
            return `
                <div class="lap-item ${isBest ? 'best' : ''}">
                    <div>
                        <div class="lap-number">Lap ${lapNumber}</div>
                        <div class="lap-info">Peak: ${lap.rssi_peak || lap.peak_rssi || 0}</div>
                    </div>
                    <div class="lap-time">${lapTime}</div>
                </div>
            `;
        }).join('');
    }
    
    updateRaceState(data) {
        this.raceActive = data.active;
        
        const startBtn = document.getElementById('startBtn');
        const stopBtn = document.getElementById('stopBtn');
        
        if (startBtn) {
            startBtn.disabled = this.raceActive;
            // Update button text based on race state
            if (this.raceActive) {
                startBtn.textContent = 'Race started';
            } else {
                startBtn.textContent = '▶ Start Race';
            }
        }
        if (stopBtn) {
            stopBtn.disabled = !this.raceActive;
        }
    }
    
    updateThresholdLines() {
        // Threshold lines are drawn on canvas in drawGraph()
        // This function triggers a redraw when thresholds change
        // The canvas reads directly from the slider values
    }
    
    drawGraph() {
        if (!this.rssiCanvas || !this.rssiCtx) {
            return;
        }
        
        const ctx = this.rssiCtx;
        const width = this.rssiCanvas.width;
        const height = this.rssiCanvas.height;
        
        // Clear canvas
        ctx.fillStyle = '#0a0e27';
        ctx.fillRect(0, 0, width, height);
        
        // Draw grid lines
        ctx.strokeStyle = 'rgba(255, 123, 0, 0.1)';
        ctx.lineWidth = 1;
        
        // Horizontal grid lines
        for (let i = 0; i <= 4; i++) {
            const y = (height / 4) * i;
            ctx.beginPath();
            ctx.moveTo(0, y);
            ctx.lineTo(width, y);
            ctx.stroke();
        }
        
        // Draw enter RSSI threshold line (orange/yellow)
        const enterRssiSlider = document.getElementById('enterRssiSlider');
        const enterRssi = enterRssiSlider ? parseInt(enterRssiSlider.value) : 120;
        const enterRssiY = height - (enterRssi / 255) * height;
        ctx.strokeStyle = 'rgba(255, 170, 0, 0.8)';
        ctx.lineWidth = 2;
        ctx.setLineDash([5, 5]);
        ctx.beginPath();
        ctx.moveTo(0, enterRssiY);
        ctx.lineTo(width, enterRssiY);
        ctx.stroke();
        ctx.setLineDash([]);
        
        // Draw exit RSSI threshold line (red/orange)
        const exitRssiSlider = document.getElementById('exitRssiSlider');
        const exitRssi = exitRssiSlider ? parseInt(exitRssiSlider.value) : 100;
        const exitRssiY = height - (exitRssi / 255) * height;
        ctx.strokeStyle = 'rgba(255, 100, 0, 0.8)';
        ctx.lineWidth = 2;
        ctx.setLineDash([3, 3]);
        ctx.beginPath();
        ctx.moveTo(0, exitRssiY);
        ctx.lineTo(width, exitRssiY);
        ctx.stroke();
        ctx.setLineDash([]);
        
        // Draw labels for thresholds
        ctx.fillStyle = 'rgba(255, 170, 0, 0.9)';
        ctx.font = '10px "Courier New", monospace';
        ctx.textAlign = 'left';
        ctx.fillText('Enter', 5, enterRssiY - 3);
        
        ctx.fillStyle = 'rgba(255, 100, 0, 0.9)';
        ctx.fillText('Exit', 5, exitRssiY - 3);
        
        // Draw RSSI line
        if (this.rssiHistory.length > 1) {
            ctx.strokeStyle = '#ff7b00';
            ctx.lineWidth = 2;
            ctx.beginPath();
            
            const pointSpacing = width / this.maxHistory;
            
            // Calculate offset so graph scrolls from right to left
            const startOffset = Math.max(0, this.maxHistory - this.rssiHistory.length);
            
            for (let i = 0; i < this.rssiHistory.length; i++) {
                const rssi = this.rssiHistory[i];
                const x = (startOffset + i) * pointSpacing;
                const y = height - (rssi / 255) * height;
                
                if (i === 0) {
                    ctx.moveTo(x, y);
                } else {
                    ctx.lineTo(x, y);
                }
            }
            
            ctx.stroke();
            
            // Draw glow effect
            ctx.shadowColor = '#ff7b00';
            ctx.shadowBlur = 10;
            ctx.stroke();
            ctx.shadowBlur = 0;
            
            // Fill area under line
            const lastX = (startOffset + this.rssiHistory.length - 1) * pointSpacing;
            const lastY = height - (this.rssiHistory[this.rssiHistory.length - 1] / 255) * height;
            ctx.lineTo(lastX, height);
            ctx.lineTo(startOffset * pointSpacing, height);
            ctx.closePath();
            
            const gradient = ctx.createLinearGradient(0, 0, 0, height);
            gradient.addColorStop(0, 'rgba(255, 123, 0, 0.3)');
            gradient.addColorStop(1, 'rgba(255, 123, 0, 0.0)');
            ctx.fillStyle = gradient;
            ctx.fill();
        }
        
        // Draw labels
        ctx.fillStyle = '#8b9dc3';
        ctx.font = '10px "Courier New", monospace';
        ctx.textAlign = 'right';
        
        // RSSI scale labels
        ctx.fillText('255', width - 5, 12);
        ctx.fillText('0', width - 5, height - 5);
        
        // Continue drawing loop
        requestAnimationFrame(() => this.drawGraph());
    }
    
    formatTime(milliseconds) {
        const totalSeconds = Math.floor(milliseconds / 1000);
        const minutes = Math.floor(totalSeconds / 60);
        const seconds = totalSeconds % 60;
        const ms = Math.floor((milliseconds % 1000) / 10);
        
        return `${minutes}:${seconds.toString().padStart(2, '0')}.${ms.toString().padStart(2, '0')}`;
    }
    
    formatTimeForSpeech(milliseconds) {
        // Convert milliseconds to human-friendly spoken time (whole seconds only for speech)
        // Sub-second precision is only shown in display/logs, not spoken
        const ms = milliseconds;
        const totalSeconds = Math.floor(ms / 1000);
        
        const hours = Math.floor(totalSeconds / 3600);
        const minutes = Math.floor((totalSeconds % 3600) / 60);
        const seconds = totalSeconds % 60; // Whole seconds only
        
        const plural = (n, s) => (n === 1 ? s : s + 's');
        
        let timeSpeech = '';
        
        if (hours > 0) {
            // For hours, include minutes and seconds (e.g., "1 hour 23 minutes 45 seconds")
            timeSpeech += `${hours} ${plural(hours, 'hour')}`;
            if (minutes > 0) {
                timeSpeech += ` ${minutes} ${plural(minutes, 'minute')}`;
            }
            if (seconds > 0) {
                timeSpeech += ` ${seconds} ${plural(seconds, 'second')}`;
            }
        } else if (minutes > 0) {
            // For minutes, include whole seconds (e.g., "1 minute 23 seconds")
            timeSpeech += `${minutes} ${plural(minutes, 'minute')}`;
            if (seconds > 0) {
                timeSpeech += ` ${seconds} ${plural(seconds, 'second')}`;
            }
        } else {
            // Less than one minute: only whole seconds (e.g., "45 seconds")
            // If less than 1 second, say "less than 1 second"
            if (seconds === 0 && ms > 0) {
                timeSpeech = 'less than 1 second';
            } else {
                timeSpeech = `${seconds} ${plural(seconds, 'second')}`;
            }
        }
        
        return timeSpeech;
    }
    
    announceLapTime(lapData) {
        if (!this.speechSynthesis) return;
        
        const lapNumber = this.laps.length;
        const lapTimeSpeech = this.formatTimeForSpeech(lapData.lap_time_ms);
        
        // Create speech text with natural language time
        let speechText = `Lap ${lapNumber}, ${lapTimeSpeech}`;
        
        // Add context about lap performance
        if (lapNumber > 1) {
            const validLaps = this.laps.filter(lap => lap.lap_time_ms > 0);
            if (validLaps.length > 1) {
                const previousLaps = validLaps.slice(0, -1);
                const bestTime = Math.min(...previousLaps.map(lap => lap.lap_time_ms));
                const currentTime = lapData.lap_time_ms;
                
                if (currentTime < bestTime) {
                    speechText += ', new best time!';
                }
                // Removed "slower lap" announcement - only announce new best times
            }
        }
        
        // Speak the announcement
        const utterance = new SpeechSynthesisUtterance(speechText);
        utterance.rate = 1.2; // Slightly faster for quick announcements
        utterance.pitch = 1.0;
        utterance.volume = 0.8;
        
        this.speechSynthesis.speak(utterance);
    }
    
    toggleAudio() {
        this.audioEnabled = !this.audioEnabled;
        const audioButton = document.getElementById('audioToggle');
        if (audioButton) {
            audioButton.textContent = this.audioEnabled ? 'Audio On' : 'Audio Off';
            audioButton.classList.toggle('active', this.audioEnabled);
        }
    }
    
    sendCommand(command, data = {}) {
        if (this.socket && this.socket.readyState === WebSocket.OPEN) {
            this.socket.send(JSON.stringify({
                command: command,
                ...data
            }));
        } else {
            console.warn('WebSocket not connected, cannot send command:', command);
        }
    }
    
    requestInitialData() {
        this.sendCommand('get_status');
        this.sendCommand('get_laps');
    }

    startRecording() {
        this.recordingActive = true;
        this.raceData.startTime = Date.now();
        this.raceData.rssiSamples = [];
        this.raceData.laps = [];
        console.log('Race recording started');
    }

    stopRecording() {
        this.recordingActive = false;
        this.raceData.endTime = Date.now();
        console.log('Race recording stopped');
    }

    clearRecording() {
        this.recordingActive = false;
        this.raceData = {
            rssiSamples: [],
            laps: [],
            startTime: null,
            endTime: null,
            enterThreshold: this.raceData.enterThreshold,
            exitThreshold: this.raceData.exitThreshold,
            frequency: this.raceData.frequency
        };
        console.log('Race recording cleared');
    }

    exportRaceDataJSON() {
        const raceExport = {
            metadata: {
                exportVersion: '1.0',
                exportDate: new Date().toISOString(),
                raceStartTime: this.raceData.startTime ? new Date(this.raceData.startTime).toISOString() : null,
                raceEndTime: this.raceData.endTime ? new Date(this.raceData.endTime).toISOString() : null,
                raceDuration: this.raceData.startTime && this.raceData.endTime ?
                    this.raceData.endTime - this.raceData.startTime : null,
                deviceName: 'StarForge Race Timer',
                enterThreshold: this.raceData.enterThreshold,
                exitThreshold: this.raceData.exitThreshold,
                frequency: this.raceData.frequency
            },
            laps: this.raceData.laps.map((lap, index) => ({
                lapNumber: index + 1,
                timestamp: lap.timestamp_ms,
                lapTime: lap.lap_time_ms,
                peakRSSI: lap.peak_rssi || lap.rssi_peak
            })),
            rssiHistory: this.raceData.rssiSamples.map(sample => ({
                timestamp: sample.timestamp,
                rssi: sample.rssi
            }))
        };

        return JSON.stringify(raceExport, null, 2);
    }

    exportRaceDataCSV() {
        let csv = 'StarForge Race Timer - Race Data Export\n';
        csv += `Export Date,${new Date().toISOString()}\n`;
        csv += `Race Start,${this.raceData.startTime ? new Date(this.raceData.startTime).toISOString() : 'N/A'}\n`;
        csv += `Race End,${this.raceData.endTime ? new Date(this.raceData.endTime).toISOString() : 'N/A'}\n`;
        csv += `Enter Threshold,${this.raceData.enterThreshold}\n`;
        csv += `Exit Threshold,${this.raceData.exitThreshold}\n`;
        csv += `Frequency,${this.raceData.frequency} MHz\n`;
        csv += '\n';

        csv += 'LAP DATA\n';
        csv += 'Lap Number,Timestamp (ms),Lap Time (ms),Peak RSSI\n';
        this.raceData.laps.forEach((lap, index) => {
            csv += `${index + 1},${lap.timestamp_ms},${lap.lap_time_ms},${lap.peak_rssi || lap.rssi_peak}\n`;
        });
        csv += '\n';

        csv += 'RSSI HISTORY\n';
        csv += 'Timestamp,RSSI\n';
        this.raceData.rssiSamples.forEach(sample => {
            csv += `${sample.timestamp},${sample.rssi}\n`;
        });

        return csv;
    }

    async downloadRaceData(format = 'both') {
        if (this.raceData.rssiSamples.length === 0 && this.raceData.laps.length === 0) {
            alert('No race data to download. Start a race and collect some data first.');
            return;
        }

        // Fetch high-resolution RSSI history from ESP32 (if available)
        try {
            const rssiHistoryResponse = await fetch('/api/rssi_history');
            if (rssiHistoryResponse.ok) {
                const rssiHistory = await rssiHistoryResponse.json();
                if (rssiHistory.samples && rssiHistory.samples.length > 0) {
                    // Merge ESP32 high-res data with browser data
                    // ESP32 uses relative timestamps (ms since race start)
                    // Browser uses absolute timestamps
                    const raceStartTime = this.raceData.startTime || Date.now();

                    // Convert ESP32 samples to browser format
                    const esp32Samples = rssiHistory.samples.map(s => ({
                        timestamp: raceStartTime + s.t,
                        rssi: s.r
                    }));

                    // Merge and sort by timestamp
                    const allSamples = [...this.raceData.rssiSamples, ...esp32Samples];
                    allSamples.sort((a, b) => a.timestamp - b.timestamp);

                    // Remove duplicates (keep ESP32 data when timestamps are close)
                    const mergedSamples = [];
                    for (let i = 0; i < allSamples.length; i++) {
                        if (i === 0 || Math.abs(allSamples[i].timestamp - allSamples[i-1].timestamp) > 10) {
                            mergedSamples.push(allSamples[i]);
                        }
                    }

                    this.raceData.rssiSamples = mergedSamples;
                    console.log(`Merged ${esp32Samples.length} ESP32 samples with ${this.raceData.rssiSamples.length} browser samples`);
                }
            }
        } catch (error) {
            console.warn('Could not fetch ESP32 RSSI history, using browser data only:', error);
        }

        const timestamp = new Date().toISOString().replace(/[:.]/g, '-').slice(0, -5);

        if (format === 'json' || format === 'both') {
            const jsonData = this.exportRaceDataJSON();
            const jsonBlob = new Blob([jsonData], { type: 'application/json' });
            const jsonUrl = URL.createObjectURL(jsonBlob);
            const jsonLink = document.createElement('a');
            jsonLink.href = jsonUrl;
            jsonLink.download = `starforge-race-${timestamp}.json`;
            jsonLink.click();
            URL.revokeObjectURL(jsonUrl);
        }

        if (format === 'csv' || format === 'both') {
            const csvData = this.exportRaceDataCSV();
            const csvBlob = new Blob([csvData], { type: 'text/csv' });
            const csvUrl = URL.createObjectURL(csvBlob);
            const csvLink = document.createElement('a');
            csvLink.href = csvUrl;
            csvLink.download = `starforge-race-${timestamp}.csv`;
            csvLink.click();
            URL.revokeObjectURL(csvUrl);
        }
    }
}

// Global functions for button handlers
let raceTimer;

function startRace() {
    console.log('Start race clicked - beginning countdown');
    
    // Disable start button during countdown
    const startBtn = document.querySelector('button[onclick="startRace()"]');
    if (startBtn) {
        startBtn.disabled = true;
        startBtn.textContent = 'Starting...';
    }
    
    // Create audio context for beeps
    const audioContext = new (window.AudioContext || window.webkitAudioContext)();
    
    // Function to play a beep
    function playBeep(duration, frequency) {
        return new Promise((resolve) => {
            const oscillator = audioContext.createOscillator();
            const gainNode = audioContext.createGain();
            
            oscillator.connect(gainNode);
            gainNode.connect(audioContext.destination);
            
            oscillator.frequency.value = frequency;
            oscillator.type = 'sine';
            
            gainNode.gain.setValueAtTime(0.3, audioContext.currentTime);
            gainNode.gain.exponentialRampToValueAtTime(0.01, audioContext.currentTime + duration);
            
            oscillator.start(audioContext.currentTime);
            oscillator.stop(audioContext.currentTime + duration);
            
            setTimeout(resolve, duration * 1000);
        });
    }
    
    // Countdown sequence
    async function countdown() {
        // Show countdown overlay
        const overlay = document.createElement('div');
        overlay.style.cssText = `
            position: fixed;
            top: 0;
            left: 0;
            width: 100%;
            height: 100%;
            background: rgba(0, 0, 0, 0.8);
            display: flex;
            align-items: center;
            justify-content: center;
            z-index: 10000;
            font-family: 'Courier New', monospace;
        `;
        
        const countdownText = document.createElement('div');
        countdownText.style.cssText = `
            font-size: 120px;
            font-weight: bold;
            color: #ff7b00;
            text-shadow: 0 0 30px #ff7b00, 0 0 60px #ff7b00;
            animation: pulse 0.5s ease-in-out;
        `;
        
        overlay.appendChild(countdownText);
        document.body.appendChild(overlay);
        
        // Countdown: 5, 4, 3, 2, 1
        for (let i = 5; i >= 1; i--) {
            countdownText.textContent = i;
            countdownText.style.animation = 'none';
            setTimeout(() => {
                countdownText.style.animation = 'pulse 0.5s ease-in-out';
            }, 10);
            
            // All countdown beeps are the same
            await playBeep(0.15, 800);
            
            await new Promise(resolve => setTimeout(resolve, 850));
        }
        
        // Show "GO!"
        countdownText.textContent = 'GO!';
        countdownText.style.color = '#00ff88';
        countdownText.style.textShadow = '0 0 30px #00ff88, 0 0 60px #00ff88';
        await playBeep(0.6, 1200);
        
        // Remove overlay after a brief moment
        await new Promise(resolve => setTimeout(resolve, 500));
        document.body.removeChild(overlay);
        
        // Start recording race data
        if (raceTimer) {
            raceTimer.startRecording();
        }

        // Actually start the race on the backend
        fetch('/api/start_race', {
            method: 'POST'
        })
        .then(response => response.json())
        .then(data => {
            console.log('Race started:', data);
            // Keep button disabled and change text to "Race started"
            if (startBtn) {
                startBtn.disabled = true;
                startBtn.textContent = 'Race started';
            }
            // Race status will be updated on next poll, but set it optimistically
            if (raceTimer) {
                raceTimer.updateRaceStatusIndicator(true, raceTimer.raceActive);
            }
        })
        .catch(error => {
            console.error('Error starting race:', error);
            // Re-enable button on error so user can try again
            if (startBtn) {
                startBtn.disabled = false;
                startBtn.textContent = '▶ Start Race';
            }
        });
    }
    
    countdown();
}

function stopRace() {
    console.log('Stop race clicked');

    // Stop recording race data
    if (raceTimer) {
        raceTimer.stopRecording();
    }

    // Show "Race Finished" overlay
    async function showRaceFinishedOverlay() {
        // Create overlay
        const overlay = document.createElement('div');
        overlay.style.cssText = `
            position: fixed;
            top: 0;
            left: 0;
            width: 100%;
            height: 100%;
            background: rgba(0, 0, 0, 0.85);
            display: flex;
            align-items: center;
            justify-content: center;
            z-index: 10000;
            font-family: 'Courier New', monospace;
            opacity: 0;
            transition: opacity 0.3s ease-in;
        `;
        
        const finishedText = document.createElement('div');
        finishedText.style.cssText = `
            font-size: 100px;
            font-weight: bold;
            color: #ff7b00;
            text-shadow: 0 0 40px #ff7b00, 0 0 80px #ff7b00, 0 0 120px rgba(255, 123, 0, 0.5);
            text-align: center;
            animation: raceFinishedPulse 0.6s ease-in-out;
        `;
        finishedText.textContent = 'RACE FINISHED';
        
        overlay.appendChild(finishedText);
        document.body.appendChild(overlay);
        
        // Fade in
        setTimeout(() => {
            overlay.style.opacity = '1';
        }, 10);
        
        // Optional: play a brief sound effect
        try {
            const audioContext = new (window.AudioContext || window.webkitAudioContext)();
            const oscillator = audioContext.createOscillator();
            const gainNode = audioContext.createGain();
            
            oscillator.connect(gainNode);
            gainNode.connect(audioContext.destination);
            
            oscillator.frequency.value = 600;
            oscillator.type = 'sine';
            
            gainNode.gain.setValueAtTime(0.2, audioContext.currentTime);
            gainNode.gain.exponentialRampToValueAtTime(0.01, audioContext.currentTime + 0.4);
            
            oscillator.start(audioContext.currentTime);
            oscillator.stop(audioContext.currentTime + 0.4);
        } catch (e) {
            // Audio context may not be available, ignore
            console.log('Audio not available for finish sound');
        }
        
        // Show for 1.5 seconds, then fade out
        await new Promise(resolve => setTimeout(resolve, 1500));
        
        overlay.style.transition = 'opacity 0.4s ease-out';
        overlay.style.opacity = '0';
        
        await new Promise(resolve => setTimeout(resolve, 400));
        if (document.body.contains(overlay)) {
            document.body.removeChild(overlay);
        }
    }

    // Start the overlay animation
    showRaceFinishedOverlay();

    fetch('/api/stop_race', {
        method: 'POST'
    })
    .then(response => response.json())
    .then(data => {
        console.log('Race stopped:', data);
        // Re-enable start button and change text back
        const startBtn = document.getElementById('startBtn');
        if (startBtn) {
            startBtn.disabled = false;
            startBtn.textContent = '▶ Start Race';
        }
        // Update race status indicator after overlay starts
        if (raceTimer) {
            raceTimer.updateRaceStatusIndicator(false, raceTimer.raceActive);
        }
    })
    .catch(error => {
        console.error('Error stopping race:', error);
        // Re-enable start button even on error
        const startBtn = document.getElementById('startBtn');
        if (startBtn) {
            startBtn.disabled = false;
            startBtn.textContent = '▶ Start Race';
        }
        // Update race status indicator even on error
        if (raceTimer) {
            raceTimer.updateRaceStatusIndicator(false, raceTimer.raceActive);
        }
    });
}

function resetRace() {
    if (confirm('Are you sure you want to reset all lap data?')) {
        fetch('/api/clear_laps', {
            method: 'POST'
        })
        .then(response => response.json())
        .then(data => {
            console.log('Race reset:', data);
            if (raceTimer) {
                raceTimer.clearLaps();  // Use method to properly reset lastLapCount
            }
        })
        .catch(error => {
            console.error('Error resetting race:', error);
        });
    }
}

function testAudio() {
    if (raceTimer && raceTimer.speechSynthesis) {
        const utterance = new SpeechSynthesisUtterance('Audio test, lap time 1 minute 23 seconds');
        utterance.rate = 1.2;
        utterance.pitch = 1.0;
        utterance.volume = 0.8;
        raceTimer.speechSynthesis.speak(utterance);
    }
}

function updateFrequency() {
    const frequency = document.getElementById('frequency').value;
    raceTimer.sendCommand('set_frequency', { frequency: parseInt(frequency) });
}

function updateThreshold() {
    // Legacy function - now uses dual threshold API
    const enterRssi = document.getElementById('enterRssiSlider')?.value || 120;
    const exitRssi = document.getElementById('exitRssiSlider')?.value || 100;
    
    fetch('/api/set_threshold', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/x-www-form-urlencoded',
        },
        body: `enter_rssi=${enterRssi}&exit_rssi=${exitRssi}`
    })
    .then(response => response.json())
    .then(data => {
        console.log('Thresholds updated:', data);
    })
    .catch(error => {
        console.error('Error updating thresholds:', error);
    });
}

// Initialize when page loads
document.addEventListener('DOMContentLoaded', () => {
    console.log('DOM loaded, initializing RaceTimer...');
    raceTimer = new RaceTimer();
    window.raceTimer = raceTimer; // Make it globally accessible
    console.log('RaceTimer initialized:', raceTimer);
});

// Handle page visibility changes
document.addEventListener('visibilitychange', () => {
    if (!document.hidden && raceTimer) {
        // Page became visible, refresh data
        raceTimer.requestInitialData();
    }
});
