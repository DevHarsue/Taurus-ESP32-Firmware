#pragma once

// ---- Toggles de hardware (independientes) ----
// Descomenta USE_OLED cuando el display SSD1306 esté conectado.
// Descomenta USE_BUZZER cuando el buzzer pasivo esté conectado.
// Si un toggle está comentado, ese periférico se emula con LEDs provisionales.
#define USE_OLED
// #define USE_BUZZER

// AS608 fingerprint sensor (UART2). Solo 4 hilos: VCC, GND, TX, RX.
// El módulo se sondea por software; no se usa pin WAKEUP/IRQ.
constexpr int AS608_RX_PIN = 16;        // ESP32 RX2 ← AS608 TX (amarillo)
constexpr int AS608_TX_PIN = 17;        // ESP32 TX2 → AS608 RX (blanco/verde)
constexpr unsigned long AS608_BAUD = 57600;

// ---- Display de estado ----
#ifdef USE_OLED
constexpr int OLED_SDA_PIN = 21;        // I2C SDA
constexpr int OLED_SCL_PIN = 22;        // I2C SCL
constexpr int OLED_WIDTH = 128;
constexpr int OLED_HEIGHT = 64;
constexpr int OLED_RESET_PIN = -1;       // -1 = comparte reset del ESP32
constexpr uint8_t OLED_I2C_ADDR = 0x3C;  // dirección I2C (0x3C típico, a veces 0x3D)
constexpr unsigned long OLED_HEALTHCHECK_INTERVAL_MS = 2000;  // re-init si pierde I2C
#else
// LEDs provisionales que reemplazan al OLED (estado WiFi/MQTT)
constexpr int LED_WIFI_PIN   = 32;
constexpr int LED_STATUS_PIN = 33;
#endif

// ---- Feedback sonoro ----
#ifdef USE_BUZZER
constexpr int BUZZER_PIN = 27;
constexpr int BUZZER_CHANNEL = 0;
#else
// LED provisional único que reemplaza al buzzer (feedback de tonos)
constexpr int LED_FEEDBACK_PIN = 25;
#endif

// Timing
constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;
constexpr unsigned long WIFI_RECONNECT_INTERVAL_MS = 5000;
constexpr unsigned long MQTT_RECONNECT_INTERVAL_MS = 3000;
constexpr unsigned long FINGERPRINT_POLL_INTERVAL_MS = 50;
constexpr unsigned long FINGERPRINT_COOLDOWN_MS = 1500;
constexpr unsigned long HEARTBEAT_INTERVAL_MS = 120000;      // 2 min
constexpr unsigned long OFFLINE_SYNC_INTERVAL_MS = 30000;    // 30s

// MQTT topics
constexpr const char* TOPIC_ACCESS_REQUEST = "gym/access/request";
constexpr const char* TOPIC_ACCESS_RESPONSE = "gym/access/response";
constexpr const char* TOPIC_DEVICE_HEARTBEAT = "gym/device/heartbeat";
constexpr const char* TOPIC_ENROLL_REQUEST = "gym/enroll/request";
constexpr const char* TOPIC_ENROLL_RESPONSE = "gym/enroll/response";
constexpr const char* TOPIC_ENROLL_DELETE = "gym/enroll/delete";

// Enrollment timeouts
constexpr unsigned long ENROLL_FINGER_TIMEOUT_MS = 15000;

// Storage
constexpr const char* OFFLINE_QUEUE_FILE = "/offline_queue.jsonl";
