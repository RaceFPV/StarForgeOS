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
        
        this.initializeUI();
        this.connectWebSocket();
    }
    
    initializeUI() {
        // Initialize threshold slider
        const thresholdSlider = document.getElementById('threshold');
        const thresholdValue = document.getElementById('thresholdValue');
        
        thresholdSlider.addEventListener('input', (e) => {
            thresholdValue.textContent = e.target.value;
            this.updateThresholdLine(e.target.value);
        });
        
        // Initialize threshold line position
        this.updateThresholdLine(thresholdSlider.value);
    }
    
    connectWebSocket() {
        // Use HTTP polling instead of WebSocket for standalone mode
        this.setConnectionStatus(true);
        this.startPolling();
    }
    
    startPolling() {
        // Poll for updates every 250ms
        this.pollInterval = setInterval(() => {
            this.updateData();
        }, 250);
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
        
        if (connected) {
            statusDot.className = 'status-dot connected';
            statusText.textContent = 'Connected';
        } else {
            statusDot.className = 'status-dot connecting';
            statusText.textContent = 'Connecting...';
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
        
        // Update RSSI displays
        document.getElementById('currentRssi').textContent = data.current_rssi || 0;
        document.getElementById('peakRssi').textContent = data.peak_rssi || 0;
        document.getElementById('lapCount').textContent = data.lap_count || 0;
        
        // Update RSSI bar
        const rssiPercent = Math.min((data.current_rssi || 0) / 255 * 100, 100);
        document.getElementById('rssiFill').style.width = rssiPercent + '%';
        
        // Show crossing state
        const statusItems = document.querySelectorAll('.status-item');
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
        const lapList = document.getElementById('lapList');
        
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
                        <div class="lap-info">Peak: ${lap.rssi_peak}</div>
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
    
    updateThresholdLine(threshold) {
        const thresholdLine = document.getElementById('thresholdLine');
        const percent = Math.min(threshold / 255 * 100, 100);
        thresholdLine.style.left = percent + '%';
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
    raceTimer.sendCommand('start_race');
}

function stopRace() {
    raceTimer.sendCommand('stop_race');
}

function resetRace() {
    if (confirm('Are you sure you want to reset all lap data?')) {
        raceTimer.sendCommand('reset_race');
        raceTimer.laps = [];
        raceTimer.updateLapDisplay();
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
    raceTimer = new RaceTimer();
});

// Handle page visibility changes
document.addEventListener('visibilitychange', () => {
    if (!document.hidden && raceTimer) {
        // Page became visible, refresh data
        raceTimer.requestInitialData();
    }
});
