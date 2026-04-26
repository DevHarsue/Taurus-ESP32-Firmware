#pragma once

// WiFi
constexpr const char* WIFI_SSID = "Hazel";
constexpr const char* WIFI_PASSWORD = "Pi=3.1416";

// MQTT (Mosquitto)
constexpr const char* MQTT_HOST = "192.168.0.103";
constexpr uint16_t MQTT_PORT = 1883;
constexpr const char* MQTT_USER = "";
constexpr const char* MQTT_PASSWORD = "";

// HTTP endpoint for offline sync
constexpr const char* API_BASE_URL = "http://192.168.0.103:8080";

// Device identity
constexpr const char* DEVICE_ID = "esp32-recepcion";

// NTP
constexpr const char* NTP_SERVER_1 = "pool.ntp.org";
constexpr const char* NTP_SERVER_2 = "time.nist.gov";
