# Beer Matrix

An ESP32-based LED matrix display for real-time homebrew keg monitoring. Drives a 128×64 RGB HUB75 panel to show keg volumes, temperatures, and pour events sourced via MQTT.

## Features

- **Multi-page display** — rotates through keg volumes, temperatures, and custom sensors with smooth slide transitions
- **Pour notifications** — interrupts normal rotation to show an active pour event (volume and elapsed time) for 12 seconds
- **Color-coded levels** — green (≥4 gal) → cyan → orange → red (<1 gal); flashing alert below 1 gallon
- **Auto-brightness** — configurable day/night schedule with automatic dim at night
- **Custom sensors** — up to 8 user-defined MQTT topics (humidity, CO2, pressure, etc.)
- **Web dashboard** — dark-themed UI on port 80 for live monitoring and configuration
- **Persistent config** — all settings stored in ESP32 flash, survives reboots
- **OTA updates** — wireless firmware updates via Arduino OTA
- **Sensor timeouts** — sensors offline for >5 minutes are greyed out

## Hardware

| Component | Details |
|-----------|---------|
| MCU | ESP32 (esp32dev) |
| Display | 128×64 RGB LED matrix, HUB75 interface |
| Framework | Arduino via PlatformIO |

### HUB75 Pin Mapping

Defined in [config.h](config.h). Default uses standard ESP32 HUB75 pinout compatible with the ESP32-HUB75-MatrixPanel-DMA library.

## MQTT Topics

| Topic | Description |
|-------|-------------|
| `beermonitor/stats` | JSON payload with tap volumes and pour state |
| `garage/beer/keg_N_temp` | Individual keg temperature (N = 1–6) |
| `garage/beer/last_pour` | Pour event notifications |
| Custom | User-defined topics configured via web dashboard |

## Display Pages

| Page | Content |
|------|---------|
| Volumes | 3×2 bar-graph grid of keg fill levels |
| Temps | Temperature for up to 6 keg sensors |
| Custom | 4×2 grid of user-defined sensor values |
| Pour | Full-screen pour alert (interrupts rotation) |

## Getting Started

### Prerequisites

- [PlatformIO](https://platformio.org/) (VS Code extension or CLI)
- ESP32 dev board
- 128×64 HUB75 RGB LED matrix panel
- MQTT broker (e.g. Mosquitto) on your local network

### Configuration

1. Copy `credsmqtt.h` and fill in your WiFi and MQTT credentials:

```cpp
#define WIFI_SSID     "your_ssid"
#define WIFI_PASSWORD "your_password"
#define MQTT_SERVER   "192.168.x.x"
#define MQTT_PORT     1883
#define MQTT_USER     "user"
#define MQTT_PASSWORD "password"
```

2. Adjust panel dimensions, brightness limits, timezone, and pin mapping in [config.h](config.h).

### Build & Flash

```bash
# Build
pio run

# Upload
pio run --target upload

# Open serial monitor
pio device monitor
```

### Web Interface

After connecting to WiFi, navigate to `http://<device-ip>/` to access the dashboard. Configuration is available at `http://<device-ip>/config`.

## Libraries

| Library | Purpose |
|---------|---------|
| ESP32 HUB75 LED Matrix Panel DMA Display | HUB75 panel driver |
| Adafruit GFX | Graphics primitives |
| PubSubClient | MQTT client |
| ArduinoJson | JSON parsing |
| ESPAsyncWebServer | Async web server |
| ESPNtpClient | NTP time sync |
| WebSockets | WebSocket support |
| JPEGDecoder | JPEG image decoding |
