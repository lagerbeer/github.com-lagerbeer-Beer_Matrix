let socket;
let reconnectAttempts = 0;
const maxReconnectAttempts = 10;

function initWebSocket() {
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const wsUrl = `${protocol}//${window.location.hostname}:81`;
    
    socket = new WebSocket(wsUrl);
    
    socket.onopen = function() {
        console.log('WebSocket connected');
        document.getElementById('connection-status').textContent = 'Connected';
        document.getElementById('connection-status').className = 'status connected';
        reconnectAttempts = 0;
    };
    
    socket.onclose = function() {
        console.log('WebSocket disconnected');
        document.getElementById('connection-status').textContent = 'Disconnected';
        document.getElementById('connection-status').className = 'status disconnected';
        
        // Attempt to reconnect
        if (reconnectAttempts < maxReconnectAttempts) {
            setTimeout(initWebSocket, 3000);
            reconnectAttempts++;
        }
    };
    
    socket.onmessage = function(event) {
        const data = JSON.parse(event.data);
        handleWebSocketMessage(data);
    };
}

function handleWebSocketMessage(data) {
    switch(data.type) {
        case 'init':
            updateSensors(data.sensors);
            updateWeather(data.weather);
            break;
            
        case 'sensor_update':
            updateSensors(data.sensors);
            break;
            
        case 'weather_update':
            updateWeather(data);
            break;
            
        case 'display_config':
            updateDisplayForm(data);
            break;
            
        case 'mqtt_config':
            updateMQTTForm(data);
            break;
            
        case 'system_info':
            updateSystemInfo(data);
            break;
    }
}

function updateSensors(sensors) {
    const grid = document.getElementById('sensor-grid');
    if (!grid) return;
    
    grid.innerHTML = '';
    
    sensors.forEach(sensor => {
        const card = document.createElement('div');
        card.className = `sensor-card ${sensor.connected ? '' : 'warning'}`;
        
        card.innerHTML = `
            <div class="sensor-name">${sensor.name}</div>
            <div class="sensor-value">
                ${sensor.value}
            </div>
        `;
        
        grid.appendChild(card);
    });
}

function updateWeather(weather) {
    const card = document.getElementById('weather-card');
    if (!card) return;
    
    if (weather.valid) {
        card.innerHTML = `
            <div class="weather-icon">🌡️</div>
            <div>
                <div class="weather-temp">${weather.temp}°C</div>
                <div class="weather-desc">${weather.description}</div>
                <div>Humidity: ${weather.humidity}%</div>
            </div>
        `;
    } else {
        card.innerHTML = '<div>Weather data unavailable</div>';
    }
}

function updateDisplayForm(config) {
    document.getElementById('brightness').value = config.brightness;
    document.getElementById('brightness-value').textContent = config.brightness;
    document.getElementById('show-date').checked = config.show_date;
    document.getElementById('show-weather').checked = config.show_weather;
    document.getElementById('show-mqtt').checked = config.show_mqtt;
    document.getElementById('cycle-interval').value = config.cycle_interval;
    document.getElementById('color-theme').value = config.color_theme;
}

function updateMQTTForm(config) {
    document.getElementById('mqtt-server').value = config.server;
    document.getElementById('mqtt-port').value = config.port;
    document.getElementById('mqtt-username').value = config.username;
    document.getElementById('mqtt-client-id').value = config.client_id;
}

function updateSystemInfo(info) {
    const uptime = info.uptime;
    const hours = Math.floor(uptime / 3600);
    const minutes = Math.floor((uptime % 3600) / 60);
    const seconds = Math.floor(uptime % 60);
    
    document.getElementById('uptime').textContent = 
        `${hours}h ${minutes}m ${seconds}s`;
    
    document.getElementById('free-heap').textContent = 
        `${info.free_heap} KB`;
    
    document.getElementById('wifi-strength').textContent = 
        `${info.wifi_strength} dBm`;
}

function showTab(tabId) {
    // Hide all tabs
    document.querySelectorAll('.tab-content').forEach(tab => {
        tab.classList.remove('active');
    });
    
    // Deactivate all tab buttons
    document.querySelectorAll('.tab-btn').forEach(btn => {
        btn.classList.remove('active');
    });
    
    // Show selected tab
    document.getElementById(tabId).classList.add('active');
    
    // Activate button
    event.target.classList.add('active');
}

function addSensor() {
    // Show modal for adding sensor
    alert('Sensor addition coming soon!');
}

function rebootDevice() {
    if (confirm('Are you sure you want to reboot the device?')) {
        fetch('/api/reboot', { method: 'POST' })
            .then(() => {
                alert('Device rebooting...');
                setTimeout(() => {
                    window.location.reload();
                }, 10000);
            });
    }
}

// Initialize on page load
document.addEventListener('DOMContentLoaded', function() {
    initWebSocket();
    
    // Display form submission
    document.getElementById('display-form').addEventListener('submit', function(e) {
        e.preventDefault();
        
        const config = {
            type: 'config_update',
            section: 'display',
            brightness: parseInt(document.getElementById('brightness').value),
            show_date: document.getElementById('show-date').checked,
            show_weather: document.getElementById('show-weather').checked,
            show_mqtt: document.getElementById('show-mqtt').checked,
            cycle_interval: parseInt(document.getElementById('cycle-interval').value),
            color_theme: document.getElementById('color-theme').value
        };
        
        socket.send(JSON.stringify(config));
    });
    
    // MQTT form submission
    document.getElementById('mqtt-form').addEventListener('submit', function(e) {
        e.preventDefault();
        
        const config = {
            type: 'config_update',
            section: 'mqtt',
            server: document.getElementById('mqtt-server').value,
            port: parseInt(document.getElementById('mqtt-port').value),
            username: document.getElementById('mqtt-username').value,
            password: document.getElementById('mqtt-password').value,
            client_id: document.getElementById('mqtt-client-id').value
        };
        
        socket.send(JSON.stringify(config));
    });
    
    // Brightness slider update
    document.getElementById('brightness').addEventListener('input', function(e) {
        document.getElementById('brightness-value').textContent = e.target.value;
    });
});