#pragma once

// WiFi
constexpr const char* WIFI_SSID = "Redmi Note 13";
constexpr const char* WIFI_PASSWORD = "qwertyuio";

// MQTT (Mosquitto)
constexpr const char* MQTT_HOST = "10.221.215.171";
constexpr uint16_t MQTT_PORT = 1883;
constexpr const char* MQTT_USER = "";
constexpr const char* MQTT_PASSWORD = "";

// HTTP endpoint for offline sync
constexpr const char* API_BASE_URL = "http://10.221.215.171:8080";

// Device identity
constexpr const char* DEVICE_ID = "esp32-recepcion";

// NTP
constexpr const char* NTP_SERVER_1 = "pool.ntp.org";
constexpr const char* NTP_SERVER_2 = "time.nist.gov";
