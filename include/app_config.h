#pragma once

// Hardware pins
constexpr int BUTTON_PIN = 18;
constexpr int BUZZER_PIN = 27;
constexpr int OLED_SDA_PIN = 21;
constexpr int OLED_SCL_PIN = 22;
constexpr int OLED_WIDTH = 128;
constexpr int OLED_HEIGHT = 64;
constexpr int OLED_RESET_PIN = -1;

// Timing
constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;
constexpr unsigned long WIFI_RECONNECT_INTERVAL_MS = 5000;
constexpr unsigned long MQTT_RECONNECT_INTERVAL_MS = 3000;
constexpr unsigned long BUTTON_DEBOUNCE_MS = 60;
constexpr unsigned long HEARTBEAT_INTERVAL_MS = 120000;      // 2 min
constexpr unsigned long OFFLINE_SYNC_INTERVAL_MS = 30000;    // 30s

// Buzzer
constexpr int BUZZER_CHANNEL = 0;

// MQTT topics
constexpr const char* TOPIC_ACCESS_REQUEST = "gym/access/request";
constexpr const char* TOPIC_ACCESS_RESPONSE = "gym/access/response";
constexpr const char* TOPIC_DEVICE_HEARTBEAT = "gym/device/heartbeat";

// Storage
constexpr const char* OFFLINE_QUEUE_FILE = "/offline_queue.jsonl";

// Fingerprints for button simulation
constexpr int SIMULATED_FINGERPRINTS[] = {1, 2, 999};
constexpr int SIMULATED_FINGERPRINTS_COUNT =
    sizeof(SIMULATED_FINGERPRINTS) / sizeof(SIMULATED_FINGERPRINTS[0]);
