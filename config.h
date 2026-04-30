#ifndef CONFIG_H
#define CONFIG_H

#include <cstdint>
#include <Arduino.h>

// Display Configuration
#define PANEL_WIDTH 128
#define PANEL_HEIGHT 64
#define DEFAULT_BRIGHTNESS 128
#define MIN_BRIGHTNESS 10
#define MAX_BRIGHTNESS 255

// Pin Configuration for HUB75 Matrix
#define R1_PIN 25
#define G1_PIN 26
#define B1_PIN 27
#define R2_PIN 14
#define G2_PIN 12
#define B2_PIN 13
#define A_PIN 23
#define B_PIN 19
#define C_PIN 5
#define D_PIN 17
#define E_PIN 18
#define LAT_PIN 4
#define OE_PIN 15
#define CLK_PIN 16

// Clock Configuration
#define TIME_ZONE -5
#define TIME_ZONE_MINUTES 0
#define USE_12_HOUR_FORMAT true

// Update Intervals (ms)
#define WEATHER_UPDATE_INTERVAL 600000  // 10 minutes
#define SENSOR_TIMEOUT 300000           // 5 minutes
#define DISPLAY_CYCLE_INTERVAL 5000      // 5 seconds

// Web Server
#define WEB_SERVER_PORT 80
#define WEB_SOCKET_PORT 81

// OpenWeatherMap Configuration
#define OPENWEATHERMAP_API_KEY "YOUR_API_KEY_HERE"
#define OPENWEATHERMAP_CITY "Your City"
#define OPENWEATHERMAP_COUNTRY_CODE "US"
#define OPENWEATHERMAP_UNITS "metric"          // metric or imperial

// WiFi Credentials - set in credsmqtt.h
#define WIFI_SSID "YOUR_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// Default MQTT Configuration - set in credsmqtt.h
#define DEFAULT_MQTT_SERVER "192.168.0.x"
#define DEFAULT_MQTT_PORT 1883
#define DEFAULT_MQTT_USERNAME "your_mqtt_user"
#define DEFAULT_MQTT_PASSWORD "your_mqtt_password"
#define DEFAULT_MQTT_CLIENT_ID "ESP32MatrixClock"

#endif