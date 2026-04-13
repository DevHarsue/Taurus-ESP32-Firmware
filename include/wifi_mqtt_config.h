#pragma once

// WiFi
constexpr const char* WIFI_SSID = "YOUR_WIFI_SSID";
constexpr const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// MQTT (Mosquitto)
constexpr const char* MQTT_HOST = "192.168.1.10";
constexpr uint16_t MQTT_PORT = 1883;
constexpr const char* MQTT_USER = "";
constexpr const char* MQTT_PASSWORD = "";

// HTTP endpoint for offline sync
constexpr const char* API_BASE_URL = "http://192.168.1.10:8080";

// Device identity
constexpr const char* DEVICE_ID = "esp32-recepcion";

// NTP
constexpr const char* NTP_SERVER_1 = "pool.ntp.org";
constexpr const char* NTP_SERVER_2 = "time.nist.gov";
