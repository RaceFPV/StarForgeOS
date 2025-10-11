#include "standalone_mode.h"
#include <WiFi.h>
#include <SPIFFS.h>
#include <ESPmDNS.h>
#include "config.h"

StandaloneMode::StandaloneMode() : _server(80), _timingCore(nullptr) {
}

void StandaloneMode::begin(TimingCore* timingCore) {
    _timingCore = timingCore;
    
    // Initialize WiFi AP with stability improvements
    setupWiFiAP();
    
    // Give WiFi time to stabilize
    delay(1000);
    
    // Initialize mDNS for .local hostname
    if (MDNS.begin(MDNS_HOSTNAME)) {
        Serial.printf("mDNS responder started: %s.local\n", MDNS_HOSTNAME);
        MDNS.addService("http", "tcp", WEB_SERVER_PORT);
    } else {
        Serial.println("Error setting up mDNS responder");
    }
    
    // Initialize SPIFFS for serving static files
    if (!SPIFFS.begin(true)) {
        Serial.println("SPIFFS Mount Failed");
        return;
    }
    
    // Setup web server routes
    _server.on("/", HTTP_GET, [this]() { handleRoot(); });
    _server.on("/api/status", HTTP_GET, [this]() { handleGetStatus(); });
    _server.on("/api/laps", HTTP_GET, [this]() { handleGetLaps(); });
    _server.on("/api/start_race", HTTP_POST, [this]() { handleStartRace(); });
    _server.on("/api/stop_race", HTTP_POST, [this]() { handleStopRace(); });
    _server.on("/api/clear_laps", HTTP_POST, [this]() { handleClearLaps(); });
    _server.on("/api/set_frequency", HTTP_POST, [this]() { handleSetFrequency(); });
    _server.on("/api/set_threshold", HTTP_POST, [this]() { handleSetThreshold(); });
    _server.on("/api/get_channels", HTTP_GET, [this]() { handleGetChannels(); });
    _server.on("/style.css", HTTP_GET, [this]() { handleStyleCSS(); });
    _server.on("/app.js", HTTP_GET, [this]() { handleAppJS(); });
    _server.onNotFound([this]() { handleNotFound(); });
    
    _server.begin();
    Serial.println("Web server started");
    Serial.printf("Access point: %s\n", WIFI_AP_SSID_PREFIX);
    Serial.printf("IP address: %s\n", WiFi.softAPIP().toString().c_str());
    Serial.printf("mDNS hostname: %s.local\n", MDNS_HOSTNAME);
    Serial.println("Open browser to http://192.168.4.1 or http://tracer.local");
    
    // Create dedicated web server task
    xTaskCreate(webServerTask, "WebServer", 8192, this, WEB_PRIORITY, &_webTaskHandle);
    Serial.println("Web server task created");
}

void StandaloneMode::process() {
    // Web server now runs in dedicated task - no need to handle here
    // mDNS handles requests automatically in background
    
    // Check for new lap data - only record during active race
    if (_raceActive && _timingCore && _timingCore->hasNewLap()) {
        LapData lap = _timingCore->getNextLap();
        
        // Store lap in our internal list (simple vector)
        _laps.push_back(lap);
        
        // Limit stored laps to prevent memory issues
        if (_laps.size() > 100) {
            _laps.erase(_laps.begin());
        }
        
        Serial.printf("Lap recorded: %dms, RSSI: %d\n", lap.timestamp_ms, lap.rssi_peak);
    }
    
    
    // Monitor WiFi status and recover if needed
    static unsigned long last_wifi_check = 0;
    if (millis() - last_wifi_check > 5000) { // Check every 5 seconds
        if (WiFi.getMode() != WIFI_AP) {
            Serial.println("WiFi mode lost, restarting...");
            WiFi.mode(WIFI_AP);
            delay(100);
        }
        last_wifi_check = millis();
    }
}

void StandaloneMode::setupWiFiAP() {
    // Disable WiFi power saving for stability
    WiFi.setSleep(false);
    
    // Set WiFi mode to AP only
    WiFi.mode(WIFI_AP);
    
    // Wait for mode to be set
    delay(100);
    
    // Create unique SSID with MAC address
    String macAddr = WiFi.macAddress();
    macAddr.replace(":", "");
    String apSSID = String(WIFI_AP_SSID_PREFIX) + "-" + macAddr.substring(8);
    
    // Configure AP with more stable settings
    WiFi.softAPConfig(
        IPAddress(192, 168, 4, 1),    // local_IP
        IPAddress(192, 168, 4, 1),    // gateway
        IPAddress(255, 255, 255, 0)   // subnet
    );
    
    // Start AP with retry logic
    bool ap_started = false;
    int retry_count = 0;
    while (!ap_started && retry_count < 5) {
        ap_started = WiFi.softAP(apSSID.c_str(), WIFI_AP_PASSWORD, 1, 0, 4); // channel 1, hidden=false, max_connections=4
        if (!ap_started) {
            Serial.printf("WiFi AP start attempt %d failed, retrying...\n", retry_count + 1);
            delay(500);
            retry_count++;
        }
    }
    
    if (ap_started) {
        Serial.printf("WiFi AP started: %s\n", apSSID.c_str());
        Serial.printf("IP address: %s\n", WiFi.softAPIP().toString().c_str());
        Serial.printf("Connected clients: %d\n", WiFi.softAPgetStationNum());
    } else {
        Serial.println("ERROR: Failed to start WiFi AP after 5 attempts");
        // Try to recover by restarting WiFi
        WiFi.disconnect(true);
        delay(1000);
        WiFi.mode(WIFI_AP);
        WiFi.softAP(apSSID.c_str(), WIFI_AP_PASSWORD);
    }
}

void StandaloneMode::handleRoot() {
    // Serve the static index.html file from SPIFFS
    _server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    _server.sendHeader("Pragma", "no-cache");
    _server.sendHeader("Expires", "0");
    
    File file = SPIFFS.open("/index.html", "r");
    if (file && file.size() > 0) {
        Serial.println("Serving index.html from SPIFFS");
        Serial.printf("File size: %d bytes\n", file.size());
        _server.streamFile(file, "text/html");
        file.close();
    } else {
        Serial.println("index.html not found or empty in SPIFFS");
        _server.send(404, "text/plain", "index.html not found");
    }
}

void StandaloneMode::handleGetStatus() {
    uint8_t current_rssi = _timingCore ? _timingCore->getCurrentRSSI() : 0;
    uint16_t frequency = _timingCore ? _timingCore->getState().frequency_mhz : 5800;
    uint8_t threshold = _timingCore ? _timingCore->getState().threshold : 50;
    bool crossing = _timingCore ? _timingCore->isCrossing() : false;
    
    // Debug output to see what values we're getting
    Serial.printf("[API] RSSI: %d, Freq: %d, Threshold: %d, Crossing: %s\n", 
                  current_rssi, frequency, threshold, crossing ? "true" : "false");
    
    
    String json = "{";
    json += "\"status\":\"" + String(_raceActive ? "racing" : "ready") + "\",";
    json += "\"lap_count\":" + String(_laps.size()) + ",";
    json += "\"uptime\":" + String(millis()) + ",";
    json += "\"rssi\":" + String(current_rssi) + ",";
    json += "\"frequency\":" + String(frequency) + ",";
    json += "\"threshold\":" + String(threshold) + ",";
    json += "\"crossing\":" + String(crossing ? "true" : "false");
    json += "}";
    
    Serial.printf("[API] JSON Response: %s\n", json.c_str());
    _server.send(200, "application/json", json);
}

void StandaloneMode::handleGetLaps() {
    String json = "[";
    
    for (size_t i = 0; i < _laps.size(); i++) {
        if (i > 0) json += ",";
        json += "{";
        json += "\"lap_number\":" + String(i + 1) + ",";
        json += "\"timestamp_ms\":" + String(_laps[i].timestamp_ms) + ",";
        json += "\"peak_rssi\":" + String(_laps[i].rssi_peak) + ",";
        
        // Calculate lap time (difference from previous lap or start)
        uint32_t lapTime = 0;
        if (i == 0) {
            lapTime = _laps[i].timestamp_ms - _raceStartTime;
        } else {
            lapTime = _laps[i].timestamp_ms - _laps[i-1].timestamp_ms;
        }
        json += "\"lap_time_ms\":" + String(lapTime);
        json += "}";
    }
    
    json += "]";
    _server.send(200, "application/json", json);
}

void StandaloneMode::handleStartRace() {
    _raceActive = true;
    _raceStartTime = millis();
    _laps.clear();
    
    Serial.println("Race started!");
    _server.send(200, "application/json", "{\"status\":\"race_started\"}");
}

void StandaloneMode::handleStopRace() {
    _raceActive = false;
    
    Serial.println("Race stopped!");
    _server.send(200, "application/json", "{\"status\":\"race_stopped\"}");
}

void StandaloneMode::handleClearLaps() {
    _laps.clear();
    
    Serial.println("Laps cleared!");
    _server.send(200, "application/json", "{\"status\":\"laps_cleared\"}");
}

void StandaloneMode::handleStyleCSS() {
    // Allow longer caching for CSS since it rarely changes
    _server.sendHeader("Cache-Control", "public, max-age=3600"); // Cache for 1 hour
    _server.sendHeader("Content-Type", "text/css");
    
    String css = R"(
body {
    font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
    margin: 0;
    padding: 20px;
    background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
    min-height: 100vh;
    color: #333;
}

.container {
    max-width: 800px;
    margin: 0 auto;
    background: white;
    border-radius: 12px;
    box-shadow: 0 8px 32px rgba(0,0,0,0.1);
    padding: 30px;
}

h1 {
    text-align: center;
    color: #2c3e50;
    margin-bottom: 30px;
    font-size: 2.5em;
    font-weight: 700;
}

.status {
    background: #f8f9fa;
    padding: 15px;
    border-radius: 8px;
    margin-bottom: 25px;
    text-align: center;
    font-weight: 600;
    border-left: 4px solid #007bff;
}

.controls {
    display: flex;
    gap: 15px;
    justify-content: center;
    margin-bottom: 30px;
    flex-wrap: wrap;
}

.btn {
    padding: 12px 24px;
    border: none;
    border-radius: 6px;
    font-size: 16px;
    font-weight: 600;
    cursor: pointer;
    transition: all 0.2s;
    min-width: 120px;
}

.btn-primary {
    background: #28a745;
    color: white;
}

.btn-primary:hover {
    background: #218838;
    transform: translateY(-1px);
}

.btn-secondary {
    background: #6c757d;
    color: white;
}

.btn-secondary:hover {
    background: #5a6268;
    transform: translateY(-1px);
}

.btn-danger {
    background: #dc3545;
    color: white;
}

.btn-danger:hover {
    background: #c82333;
    transform: translateY(-1px);
}

.stats {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(150px, 1fr));
    gap: 20px;
    margin-bottom: 30px;
}

.stat-card {
    background: #f8f9fa;
    padding: 20px;
    border-radius: 8px;
    text-align: center;
    border: 1px solid #e9ecef;
}

.stat-number {
    font-size: 2em;
    font-weight: 700;
    color: #007bff;
    margin-bottom: 5px;
}

.stat-label {
    color: #6c757d;
    font-size: 0.9em;
    text-transform: uppercase;
    letter-spacing: 0.5px;
}

.laps-section h2 {
    color: #2c3e50;
    margin-bottom: 20px;
    padding-bottom: 10px;
    border-bottom: 2px solid #e9ecef;
}

.laps-container {
    max-height: 400px;
    overflow-y: auto;
}

.lap-item {
    background: #f8f9fa;
    margin-bottom: 10px;
    padding: 15px;
    border-radius: 6px;
    border-left: 4px solid #007bff;
    display: flex;
    justify-content: space-between;
    align-items: center;
}

.lap-number {
    font-weight: 700;
    color: #007bff;
}

.lap-time {
    font-weight: 600;
    font-family: 'Courier New', monospace;
}

.lap-rssi {
    color: #6c757d;
    font-size: 0.9em;
}

.no-laps {
    text-align: center;
    color: #6c757d;
    font-style: italic;
    padding: 40px;
}

.config-section {
    background: #f8f9fa;
    padding: 20px;
    border-radius: 8px;
    margin-bottom: 25px;
    border: 1px solid #e9ecef;
}

.config-section h3 {
    margin-top: 0;
    margin-bottom: 15px;
    color: #2c3e50;
}

.config-row {
    display: flex;
    gap: 20px;
    margin-bottom: 15px;
    flex-wrap: wrap;
}

.config-item {
    flex: 1;
    min-width: 200px;
}

.config-item label {
    display: block;
    margin-bottom: 5px;
    font-weight: 600;
    color: #495057;
}

.config-item select, .config-item input[type="range"] {
    width: 100%;
    padding: 8px;
    border: 1px solid #ced4da;
    border-radius: 4px;
    font-size: 14px;
}

.rssi-display {
    text-align: center;
}

.rssi-value {
    font-size: 32px;
    font-weight: 700;
    color: #007bff;
    display: block;
    margin-bottom: 10px;
    text-shadow: 0 2px 4px rgba(0,0,0,0.1);
    transition: all 0.3s ease;
}

.rssi-bar {
    position: relative;
    width: 100%;
    height: 20px;
    background: #e9ecef;
    border-radius: 10px;
    overflow: hidden;
}

.rssi-level {
    height: 100%;
    background: linear-gradient(to right, #28a745, #ffc107, #dc3545);
    width: 0%;
    transition: width 0.3s ease;
}

.threshold-line {
    position: absolute;
    top: 0;
    bottom: 0;
    width: 2px;
    background: #dc3545;
    left: 20%;
    transition: left 0.3s ease;
}

#thresholdValue {
    display: inline-block;
    margin-left: 10px;
    font-weight: 600;
    color: #007bff;
}

.rssi-warning {
    background: #fff3cd;
    border: 2px solid #ffc107;
    border-radius: 8px;
    padding: 15px;
    margin-bottom: 20px;
    text-align: center;
    animation: pulse 2s infinite;
}

.warning-content {
    display: flex;
    align-items: center;
    justify-content: center;
    gap: 10px;
}

.warning-icon {
    font-size: 1.5em;
}

.warning-text {
    font-weight: 700;
    color: #856404;
    font-size: 1.1em;
}

@keyframes pulse {
    0%, 100% { opacity: 1; }
    50% { opacity: 0.7; }
}

@media (max-width: 600px) {
    .container {
        padding: 20px;
        margin: 10px;
    }
    
    .controls {
        flex-direction: column;
        align-items: center;
    }
    
    .btn {
        width: 100%;
        max-width: 200px;
    }
}
)";
    _server.send(200, "text/css", css);
}

void StandaloneMode::handleAppJS() {
    // Serial.println("[DEBUG] Serving app.js file"); // Reduced debug output
    
    // Allow longer caching for JS since it rarely changes
    _server.sendHeader("Cache-Control", "public, max-age=3600"); // Cache for 1 hour
    _server.sendHeader("Content-Type", "application/javascript");
    
    String js = R"(
console.log('=== JavaScript file loading ===');
console.log('Document ready state:', document.readyState);
console.log('Document body:', document.body);
console.log('All elements with ID:', document.querySelectorAll('[id]'));

let raceActive = false;
let updateInterval;
let channelData = {};

console.log('=== Setting up DOMContentLoaded listener ===');

document.addEventListener('DOMContentLoaded', () => {
    console.log('=== DOM CONTENT LOADED EVENT FIRED ===');
    console.log('DOM loaded, checking for elements...');
    console.log('currentRSSI element:', document.getElementById('currentRSSI'));
    console.log('status element:', document.getElementById('status'));
    console.log('rssiLevel element:', document.getElementById('rssiLevel'));
    console.log('thresholdSlider element:', document.getElementById('thresholdSlider'));
    console.log('All elements in document:', document.querySelectorAll('*').length);
    
    // Small delay to ensure all elements are ready
    setTimeout(() => {
        console.log('=== Running delayed initialization ===');
        console.log('bandSelect element:', document.getElementById('bandSelect'));
        console.log('channelSelect element:', document.getElementById('channelSelect'));
        
        // Initialize channels with all bands available
        updateChannels();
        
        // Initialize with default Raceband channels
        updateData();
        startPeriodicUpdates();
        
        // Set initial frequency
        setFrequency();
    }, 100);
});

console.log('=== JavaScript file loaded completely ===');

async function loadChannelData() {
    try {
        const response = await fetch('/api/get_channels');
        channelData = await response.json();
        updateChannels(); // Initialize with Raceband
        setFrequency(); // Set initial frequency
    } catch (error) {
        console.error('Error loading channel data:', error);
        // updateChannels() will handle the fallback automatically
        updateChannels();
    }
}

function updateChannels() {
    console.log('=== updateChannels() called ===');
    const bandSelect = document.getElementById('bandSelect');
    const channelSelect = document.getElementById('channelSelect');
    
    console.log('bandSelect found:', !!bandSelect);
    console.log('channelSelect found:', !!channelSelect);
    
    // Safety check - make sure elements exist
    if (!bandSelect || !channelSelect) {
        console.log('Elements not ready yet, skipping updateChannels');
        return;
    }
    
    const selectedBand = bandSelect.value;
    console.log('Selected band:', selectedBand);
    
    // Clear current options
    channelSelect.innerHTML = '';
    
    // Define all channel data locally for reliability
    const allChannels = {
        'Raceband': [
            {channel: 'R1', frequency: 5658},
            {channel: 'R2', frequency: 5695},
            {channel: 'R3', frequency: 5732},
            {channel: 'R4', frequency: 5769},
            {channel: 'R5', frequency: 5806},
            {channel: 'R6', frequency: 5843},
            {channel: 'R7', frequency: 5880},
            {channel: 'R8', frequency: 5917}
        ],
        'Fatshark': [
            {channel: 'F1', frequency: 5740},
            {channel: 'F2', frequency: 5760},
            {channel: 'F3', frequency: 5780},
            {channel: 'F4', frequency: 5800},
            {channel: 'F5', frequency: 5820},
            {channel: 'F6', frequency: 5840},
            {channel: 'F7', frequency: 5860},
            {channel: 'F8', frequency: 5880}
        ],
        'Boscam_A': [
            {channel: 'A1', frequency: 5865},
            {channel: 'A2', frequency: 5845},
            {channel: 'A3', frequency: 5825},
            {channel: 'A4', frequency: 5805},
            {channel: 'A5', frequency: 5785},
            {channel: 'A6', frequency: 5765},
            {channel: 'A7', frequency: 5745},
            {channel: 'A8', frequency: 5725}
        ],
        'Boscam_E': [
            {channel: 'E1', frequency: 5705},
            {channel: 'E2', frequency: 5685},
            {channel: 'E3', frequency: 5665},
            {channel: 'E4', frequency: 5645},
            {channel: 'E5', frequency: 5885},
            {channel: 'E6', frequency: 5905},
            {channel: 'E7', frequency: 5925},
            {channel: 'E8', frequency: 5945}
        ]
    };
    
    // Get channels for selected band
    const channels = allChannels[selectedBand] || allChannels['Raceband'];
    
    // Populate the dropdown
    channels.forEach(channel => {
        const option = document.createElement('option');
        option.value = channel.frequency;
        option.textContent = `${channel.channel} (${channel.frequency} MHz)`;
        channelSelect.appendChild(option);
    });
    
    // Automatically set the frequency when channels change
    setFrequency();
}

async function setFrequency() {
    const channelSelect = document.getElementById('channelSelect');
    const frequency = channelSelect.value;
    
    try {
        const response = await fetch('/api/set_frequency', {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: `frequency=${frequency}`
        });
        
        if (response.ok) {
            console.log(`Frequency set to ${frequency} MHz`);
        }
    } catch (error) {
        console.error('Error setting frequency:', error);
    }
}

// Visual update only - called on every drag (smooth, no server calls)
function updateThresholdVisual(value) {
    document.getElementById('thresholdValue').textContent = value;
    
    // Update threshold line position (0-255 -> 0-100%)
    const thresholdLine = document.getElementById('thresholdLine');
    const percentage = (value / 255) * 100;
    thresholdLine.style.left = percentage + '%';
}

// Server update only - called when user releases slider (debounced)
async function updateThresholdServer(value) {
    try {
        const response = await fetch('/api/set_threshold', {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: `threshold=${value}`
        });
        
        if (response.ok) {
            console.log(`Threshold set to ${value}`);
        }
    } catch (error) {
        console.error('Error setting threshold:', error);
    }
}

function startPeriodicUpdates() {
    updateInterval = setInterval(updateData, 250); // Update every 250ms for responsive RSSI
}

async function updateData() {
    try {
        // Update status
        const statusResponse = await fetch('/api/status');
        const status = await statusResponse.json();
        
        // console.log('API Response:', status); // Debug output - disabled
        
        // Update RSSI display first
        const currentRSSI = status.rssi || 0;
        const rssiElement = document.getElementById('currentRSSI');
        
        if (rssiElement) {
            rssiElement.textContent = currentRSSI;
            // console.log('Updated RSSI display to:', currentRSSI); // Debug output - disabled
        } else {
            console.error('Could not find currentRSSI element!');
        }
        
        // Show/hide RSSI warning
        
        // Update status text with more detailed info
        const crossingStatus = status.crossing ? ' | CROSSING!' : '';
        const statusText = `Status: ${status.status} | Freq: ${status.frequency} MHz | Threshold: ${status.threshold} | RSSI: ${currentRSSI}${crossingStatus}`;
        const statusElement = document.getElementById('status');
        
        if (statusElement) {
            statusElement.textContent = statusText;
        } else {
            console.error('Could not find status element!');
        }
        
        // Add visual feedback for crossing state
        if (statusElement) {
            if (status.crossing) {
                statusElement.style.borderLeft = '4px solid #dc3545';
                statusElement.style.backgroundColor = '#f8d7da';
            } else {
                statusElement.style.borderLeft = '4px solid #007bff';
                statusElement.style.backgroundColor = '#f8f9fa';
            }
        }
        
        // Add visual feedback when RSSI is above threshold
        if (rssiElement) {
            if (currentRSSI > status.threshold) {
                rssiElement.style.color = '#dc3545'; // Red when above threshold
                rssiElement.style.fontWeight = 'bold';
            } else {
                rssiElement.style.color = '#007bff'; // Blue when below threshold
                rssiElement.style.fontWeight = '700';
            }
        }
        
        // Update RSSI bar (0-255 -> 0-100%)
        const rssiLevel = document.getElementById('rssiLevel');
        const rssiPercentage = (currentRSSI / 255) * 100;
        
        if (rssiLevel) {
            rssiLevel.style.width = rssiPercentage + '%';
            
            // Change RSSI bar color based on level
            if (rssiPercentage < 20) {
                rssiLevel.style.background = '#28a745'; // Green for low
            } else if (rssiPercentage < 60) {
                rssiLevel.style.background = 'linear-gradient(to right, #28a745, #ffc107)'; // Green to yellow
            } else {
                rssiLevel.style.background = 'linear-gradient(to right, #ffc107, #dc3545)'; // Yellow to red
            }
        } else {
            console.error('Could not find rssiLevel element!');
        }
        
        // Update threshold slider if it doesn't match
        const thresholdSlider = document.getElementById('thresholdSlider');
        if (status.threshold && parseInt(thresholdSlider.value) !== status.threshold) {
            thresholdSlider.value = status.threshold;
            document.getElementById('thresholdValue').textContent = status.threshold;
            const thresholdLine = document.getElementById('thresholdLine');
            const percentage = (status.threshold / 255) * 100;
            thresholdLine.style.left = percentage + '%';
        }
        
        // Update laps
        const lapsResponse = await fetch('/api/laps');
        const laps = await lapsResponse.json();
        
        updateLapsDisplay(laps);
        updateStats(laps);
        
    } catch (error) {
        console.error('Error updating data:', error);
        document.getElementById('status').textContent = 'Status: Connection Error';
    }
}

function updateLapsDisplay(laps) {
    const lapsContainer = document.getElementById('laps');
    
    // Check for new laps - only announce during active race
    if (raceActive && laps.length > previousLapCount) {
        const newLap = laps[laps.length - 1];
        if (audioEnabled && newLap.lap_time_ms > 0) {
            announceLapTime(newLap);
            
            // Announce lap milestones
            if (laps.length % 5 === 0) {
                announceLapMilestone(laps.length);
            }
        }
    }
    previousLapCount = laps.length;
    
    if (laps.length === 0) {
        lapsContainer.innerHTML = '<p class="no-laps">No laps recorded yet</p>';
        return;
    }
    
    let html = '';
    laps.forEach((lap, index) => {
        html += `
            <div class="lap-item">
                <div class="lap-number">Lap ${lap.lap_number}</div>
                <div class="lap-time">${formatLapTime(lap.lap_time_ms)}</div>
                <div class="lap-rssi">RSSI: ${lap.peak_rssi}</div>
            </div>
        `;
    });
    
    lapsContainer.innerHTML = html;
}

function updateStats(laps) {
    document.getElementById('lapCount').textContent = laps.length;
    
    if (laps.length === 0) {
        document.getElementById('bestLap').textContent = '--:--';
        document.getElementById('lastLap').textContent = '--:--';
        return;
    }
    
    // Find best lap time
    let bestTime = Math.min(...laps.map(lap => lap.lap_time_ms));
    document.getElementById('bestLap').textContent = formatLapTime(bestTime);
    
    // Show last lap time
    let lastTime = laps[laps.length - 1].lap_time_ms;
    document.getElementById('lastLap').textContent = formatLapTime(lastTime);
}

async function startRace() {
    try {
        const response = await fetch('/api/start_race', { method: 'POST' });
        if (response.ok) {
            raceActive = true;
            updateData();
            
            // Announce race start
            if (audioEnabled && window.speechSynthesis) {
                const utterance = new SpeechSynthesisUtterance('Race started!');
                utterance.rate = 1.0;
                utterance.pitch = 1.1;
                utterance.volume = 0.9;
                window.speechSynthesis.speak(utterance);
            }
        }
    } catch (error) {
        console.error('Error starting race:', error);
    }
}

async function stopRace() {
    try {
        const response = await fetch('/api/stop_race', { method: 'POST' });
        if (response.ok) {
            raceActive = false;
            updateData();
            
            // Announce race stop
            if (audioEnabled && window.speechSynthesis) {
                const utterance = new SpeechSynthesisUtterance('Race stopped!');
                utterance.rate = 1.0;
                utterance.pitch = 0.9;
                utterance.volume = 0.9;
                window.speechSynthesis.speak(utterance);
            }
        }
    } catch (error) {
        console.error('Error stopping race:', error);
    }
}

async function clearLaps() {
    if (confirm('Clear all lap data?')) {
        try {
            const response = await fetch('/api/clear_laps', { method: 'POST' });
            if (response.ok) {
                updateData();
            }
        } catch (error) {
            console.error('Error clearing laps:', error);
        }
    }
}

// Audio control functions
let audioEnabled = true;
let previousLapCount = 0;

function toggleAudio() {
    audioEnabled = !audioEnabled;
    const audioButton = document.getElementById('audioToggle');
    if (audioButton) {
        audioButton.textContent = audioEnabled ? 'Audio On' : 'Audio Off';
        audioButton.classList.toggle('active', audioEnabled);
    }
}

function testAudio() {
    if (window.speechSynthesis) {
        const utterance = new SpeechSynthesisUtterance('Audio test, lap time 1 minute 23 seconds');
        utterance.rate = 1.2;
        utterance.pitch = 1.0;
        utterance.volume = 0.8;
        window.speechSynthesis.speak(utterance);
    } else {
        alert('Speech synthesis not supported in this browser');
    }
}


function announceLapTime(lapData) {
    if (!audioEnabled || !window.speechSynthesis) return;
    
    const lapNumber = lapData.lap_number || 1;
    const lapTime = formatLapTime(lapData.lap_time_ms);
    
    // Create speech text - clean and simple
    const speechText = `Lap ${lapNumber}, ${lapTime}`;
    
    // Speak the announcement
    const utterance = new SpeechSynthesisUtterance(speechText);
    utterance.rate = 1.2;
    utterance.pitch = 1.0;
    utterance.volume = 0.8;
    
    window.speechSynthesis.speak(utterance);
}

function announceLapMilestone(lapCount) {
    if (!audioEnabled || !window.speechSynthesis) return;
    
    let speechText;
    if (lapCount === 5) {
        speechText = '5 laps completed!';
    } else if (lapCount === 10) {
        speechText = '10 laps completed!';
    } else if (lapCount === 15) {
        speechText = '15 laps completed!';
    } else if (lapCount === 20) {
        speechText = '20 laps completed!';
    } else {
        speechText = `${lapCount} laps completed!`;
    }
    
    // Speak the milestone announcement
    const utterance = new SpeechSynthesisUtterance(speechText);
    utterance.rate = 1.0;
    utterance.pitch = 1.2;
    utterance.volume = 0.9;
    
    // Add a small delay to avoid overlapping with lap time announcement
    setTimeout(() => {
        window.speechSynthesis.speak(utterance);
    }, 500);
}

function formatTime(ms) {
    const seconds = Math.floor(ms / 1000);
    const minutes = Math.floor(seconds / 60);
    const hours = Math.floor(minutes / 60);
    
    if (hours > 0) {
        return `${hours}:${(minutes % 60).toString().padStart(2, '0')}:${(seconds % 60).toString().padStart(2, '0')}`;
    } else if (minutes > 0) {
        return `${minutes}:${(seconds % 60).toString().padStart(2, '0')}`;
    } else {
        return `${seconds}s`;
    }
}

function formatLapTime(ms) {
    const totalSeconds = ms / 1000;
    const minutes = Math.floor(totalSeconds / 60);
    const seconds = (totalSeconds % 60).toFixed(3);
    
    if (minutes > 0) {
        return `${minutes}:${seconds.padStart(6, '0')}`;
    } else {
        return `${seconds}s`;
    }
}
)";
    _server.send(200, "application/javascript", js);
}

void StandaloneMode::handleNotFound() {
    _server.send(404, "text/plain", "File not found");
}

void StandaloneMode::handleSetFrequency() {
    if (_server.hasArg("frequency")) {
        int freq = _server.arg("frequency").toInt();
        if (freq >= 5645 && freq <= 5945) {
            if (_timingCore) {
                _timingCore->setFrequency(freq);
            }
            _server.send(200, "application/json", "{\"status\":\"frequency_set\",\"frequency\":" + String(freq) + "}");
            Serial.printf("Frequency set to: %d MHz\n", freq);
        } else {
            _server.send(400, "application/json", "{\"error\":\"invalid_frequency\"}");
        }
    } else {
        _server.send(400, "application/json", "{\"error\":\"missing_frequency\"}");
    }
}

void StandaloneMode::handleSetThreshold() {
    if (_server.hasArg("threshold")) {
        int threshold = _server.arg("threshold").toInt();
        if (threshold >= 0 && threshold <= 255) {
            if (_timingCore) {
                _timingCore->setThreshold(threshold);
            }
            _server.send(200, "application/json", "{\"status\":\"threshold_set\",\"threshold\":" + String(threshold) + "}");
            Serial.printf("Threshold set to: %d\n", threshold);
        } else {
            _server.send(400, "application/json", "{\"error\":\"invalid_threshold\"}");
        }
    } else {
        _server.send(400, "application/json", "{\"error\":\"missing_threshold\"}");
    }
}

void StandaloneMode::handleGetChannels() {
    String json = "{";
    json += "\"bands\":{";
    
    // Raceband (R1-R8)
    json += "\"Raceband\":[";
    int raceband[] = {5658, 5695, 5732, 5769, 5806, 5843, 5880, 5917};
    for (int i = 0; i < 8; i++) {
        if (i > 0) json += ",";
        json += "{\"channel\":\"R" + String(i+1) + "\",\"frequency\":" + String(raceband[i]) + "}";
    }
    json += "],";
    
    // Fatshark (F1-F8)
    json += "\"Fatshark\":[";
    int fatshark[] = {5740, 5760, 5780, 5800, 5820, 5840, 5860, 5880};
    for (int i = 0; i < 8; i++) {
        if (i > 0) json += ",";
        json += "{\"channel\":\"F" + String(i+1) + "\",\"frequency\":" + String(fatshark[i]) + "}";
    }
    json += "],";
    
    // Boscam A (A1-A8)
    json += "\"Boscam_A\":[";
    int boscam_a[] = {5865, 5845, 5825, 5805, 5785, 5765, 5745, 5725};
    for (int i = 0; i < 8; i++) {
        if (i > 0) json += ",";
        json += "{\"channel\":\"A" + String(i+1) + "\",\"frequency\":" + String(boscam_a[i]) + "}";
    }
    json += "],";
    
    // Boscam E (E1-E8)
    json += "\"Boscam_E\":[";
    int boscam_e[] = {5705, 5685, 5665, 5645, 5885, 5905, 5925, 5945};
    for (int i = 0; i < 8; i++) {
        if (i > 0) json += ",";
        json += "{\"channel\":\"E" + String(i+1) + "\",\"frequency\":" + String(boscam_e[i]) + "}";
    }
    json += "]";
    
    json += "}}";
    
    _server.send(200, "application/json", json);
}


// Web server task - runs at WEB_PRIORITY (2) - below timing (3)
void StandaloneMode::webServerTask(void* parameter) {
    StandaloneMode* mode = static_cast<StandaloneMode*>(parameter);
    unsigned long last_wifi_check = 0;
    
    while (true) {
        // Handle web server requests
        mode->_server.handleClient();
        
        // Check WiFi status every 10 seconds in web server task
        if (millis() - last_wifi_check > 10000) {
            if (WiFi.getMode() != WIFI_AP) {
                Serial.println("[WebServer] WiFi mode lost, attempting recovery...");
                WiFi.mode(WIFI_AP);
                delay(100);
            }
            last_wifi_check = millis();
        }
        
        // Small delay to prevent task from consuming all CPU
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

