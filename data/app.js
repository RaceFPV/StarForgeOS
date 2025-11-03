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
        
        // RSSI Graph variables
        this.rssiHistory = [];
        this.maxHistory = 150; // 150 points for scrolling graph
        this.rssiCanvas = null;
        this.rssiCtx = null;
        
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
        // Initialize threshold slider
        const thresholdSlider = document.getElementById('thresholdSlider');
        const thresholdValue = document.getElementById('thresholdValue');
        
        if (thresholdSlider && thresholdValue) {
            thresholdSlider.addEventListener('input', (e) => {
                thresholdValue.textContent = e.target.value;
                this.updateThresholdLine(e.target.value);
            });
            
            // Initialize threshold line position
            this.updateThresholdLine(thresholdSlider.value);
        }
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
            // Get status
            const statusResponse = await fetch('/api/status');
            const status = await statusResponse.json();
            this.updateStatus(status);
            
            // Get laps
            const lapsResponse = await fetch('/api/laps');
            const laps = await lapsResponse.json();
            this.updateLaps(laps);
            
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
        this.updateLapDisplay();
    }
    
    addLap(lapData) {
        this.laps.push(lapData);
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
        
        if (startBtn) startBtn.disabled = this.raceActive;
        if (stopBtn) stopBtn.disabled = !this.raceActive;
    }
    
    updateThresholdLine(threshold) {
        // Threshold line is now drawn on canvas in drawGraph()
        // This function is kept for compatibility but doesn't need to do anything
        // The canvas reads directly from the slider value
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
        
        // Draw threshold line (from slider)
        const thresholdSlider = document.getElementById('thresholdSlider');
        const threshold = thresholdSlider ? parseInt(thresholdSlider.value) : 50;
        const thresholdY = height - (threshold / 255) * height;
        ctx.strokeStyle = 'rgba(255, 170, 0, 0.8)';
        ctx.lineWidth = 2;
        ctx.setLineDash([5, 5]);
        ctx.beginPath();
        ctx.moveTo(0, thresholdY);
        ctx.lineTo(width, thresholdY);
        ctx.stroke();
        ctx.setLineDash([]);
        
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
            // Get status
            const statusResponse = await fetch('/api/status');
            const status = await statusResponse.json();
            this.updateStatus(status);
            
            // Get laps
            const lapsResponse = await fetch('/api/laps');
            const laps = await lapsResponse.json();
            this.updateLaps(laps);
            
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
        this.updateLapDisplay();
    }
    
    addLap(lapData) {
        this.laps.push(lapData);
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
        if (lapData.lap_time_ms > 0) {
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
        
        startBtn.disabled = this.raceActive;
        stopBtn.disabled = !this.raceActive;
    }
    
    formatTime(milliseconds) {
        const totalSeconds = Math.floor(milliseconds / 1000);
        const minutes = Math.floor(totalSeconds / 60);
        const seconds = totalSeconds % 60;
        const ms = Math.floor((milliseconds % 1000) / 10);
        
        return `${minutes}:${seconds.toString().padStart(2, '0')}.${ms.toString().padStart(2, '0')}`;
    }
    
    announceLapTime(lapData) {
        if (!this.speechSynthesis) return;
        
        const lapNumber = this.laps.length;
        const lapTime = this.formatTime(lapData.lap_time_ms);
        
        // Create speech text
        let speechText = `Lap ${lapNumber}, ${lapTime}`;
        
        // Add context about lap performance
        if (lapNumber > 1) {
            const validLaps = this.laps.filter(lap => lap.lap_time_ms > 0);
            if (validLaps.length > 1) {
                const previousLaps = validLaps.slice(0, -1);
                const bestTime = Math.min(...previousLaps.map(lap => lap.lap_time_ms));
                const currentTime = lapData.lap_time_ms;
                
                if (currentTime < bestTime) {
                    speechText += ', new best time!';
                } else if (currentTime > bestTime * 1.1) {
                    speechText += ', slower lap';
                }
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
        
        // Actually start the race on the backend
        fetch('/api/start_race', {
            method: 'POST'
        })
        .then(response => response.json())
        .then(data => {
            console.log('Race started:', data);
            if (startBtn) {
                startBtn.disabled = false;
                startBtn.textContent = '▶ Start Race';
            }
        })
        .catch(error => {
            console.error('Error starting race:', error);
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
    fetch('/api/stop_race', {
        method: 'POST'
    })
    .then(response => response.json())
    .then(data => {
        console.log('Race stopped:', data);
    })
    .catch(error => {
        console.error('Error stopping race:', error);
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
                raceTimer.laps = [];
                raceTimer.updateLapDisplay();
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
    const threshold = document.getElementById('threshold').value;
    raceTimer.sendCommand('set_threshold', { threshold: parseInt(threshold) });
    raceTimer.updateThresholdLine(threshold);
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
