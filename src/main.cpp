#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <SPIFFS.h>
#include <time.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "app_config.h"
#include "wifi_mqtt_config.h"

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
Adafruit_SSD1306 oled(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET_PIN);

unsigned long lastWiFiReconnectAttemptMs = 0;
unsigned long lastMqttReconnectAttemptMs = 0;
unsigned long lastHeartbeatMs = 0;
unsigned long lastOfflineSyncAttemptMs = 0;
unsigned long lastButtonEdgeMs = 0;

bool lastButtonState = HIGH;
int simulatedFingerprintIndex = 0;

void drawStatus(
    const String& line1,
    const String& line2 = "",
    const String& line3 = ""
) {
    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);
    oled.setCursor(0, 0);
    oled.println(line1);
    oled.println(line2);
    oled.println(line3);
    oled.display();
}

void beep(uint16_t frequency, uint16_t durationMs) {
    ledcWriteTone(BUZZER_CHANNEL, frequency);
    delay(durationMs);
    ledcWriteTone(BUZZER_CHANNEL, 0);
}

void playGrantedTone() {
    beep(1800, 90);
    delay(40);
    beep(2200, 110);
}

void playDeniedTone() {
    beep(500, 220);
}

void playInfoTone() {
    beep(1300, 70);
}

String isoTimestampNow() {
    const time_t now = time(nullptr);
    if (now < 100000) {
        const unsigned long uptimeSeconds = millis() / 1000;
        char fallback[25];
        snprintf(
            fallback,
            sizeof(fallback),
            "1970-01-01T00:%02lu:%02luZ",
            (uptimeSeconds / 60) % 60,
            uptimeSeconds % 60
        );
        return String(fallback);
    }

    struct tm utcTime;
    gmtime_r(&now, &utcTime);
    char iso[25];
    strftime(iso, sizeof(iso), "%Y-%m-%dT%H:%M:%SZ", &utcTime);
    return String(iso);
}

bool appendOfflineEvent(
    int fingerprintId,
    const String& timestampIso,
    const String& deviceId
) {
    File file = SPIFFS.open(OFFLINE_QUEUE_FILE, FILE_APPEND);
    if (!file) {
        return false;
    }

    JsonDocument doc;
    doc["fingerprint_id"] = fingerprintId;
    doc["timestamp"] = timestampIso;
    doc["device_id"] = deviceId;

    String line;
    serializeJson(doc, line);
    file.println(line);
    file.close();
    return true;
}

size_t loadOfflineEvents(JsonArray outputArray) {
    if (!SPIFFS.exists(OFFLINE_QUEUE_FILE)) {
        return 0;
    }

    File file = SPIFFS.open(OFFLINE_QUEUE_FILE, FILE_READ);
    if (!file) {
        return 0;
    }

    size_t count = 0;
    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        if (line.isEmpty()) {
            continue;
        }

        JsonDocument lineDoc;
        const DeserializationError err = deserializeJson(lineDoc, line);
        if (err) {
            continue;
        }

        JsonObject item = outputArray.add<JsonObject>();
        item["fingerprint_id"] = lineDoc["fingerprint_id"] | 0;
        item["timestamp"] = lineDoc["timestamp"] | "";
        item["device_id"] = lineDoc["device_id"] | DEVICE_ID;
        count++;
    }

    file.close();
    return count;
}

void clearOfflineQueue() {
    if (SPIFFS.exists(OFFLINE_QUEUE_FILE)) {
        SPIFFS.remove(OFFLINE_QUEUE_FILE);
    }
}

void ensureTimeSynced() {
    configTime(0, 0, NTP_SERVER_1, NTP_SERVER_2);
}

void connectWiFi() {
    if (WiFi.status() == WL_CONNECTED) {
        return;
    }

    drawStatus("WiFi conectando...", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    const unsigned long start = millis();
    while (
        WiFi.status() != WL_CONNECTED &&
        millis() - start < WIFI_CONNECT_TIMEOUT_MS
    ) {
        delay(300);
    }

    if (WiFi.status() == WL_CONNECTED) {
        ensureTimeSynced();
        drawStatus("WiFi conectado", WiFi.localIP().toString());
        playInfoTone();
    } else {
        drawStatus("WiFi sin conexion");
    }
}

void onMqttMessage(char* topic, byte* payload, unsigned int length) {
    String topicName(topic);
    String message;
    message.reserve(length);

    for (unsigned int i = 0; i < length; i++) {
        message += static_cast<char>(payload[i]);
    }

    if (topicName != TOPIC_ACCESS_RESPONSE) {
        return;
    }

    JsonDocument response;
    const DeserializationError err = deserializeJson(response, message);
    if (err) {
        drawStatus("MQTT resp invalida", err.c_str());
        playDeniedTone();
        return;
    }

    const bool granted = response["granted"] | false;
    const String name = String(static_cast<const char*>(response["name"] | ""));
    const int daysLeft = response["days_left"] | 0;
    const String reason = String(static_cast<const char*>(response["reason"] | ""));

    drawStatus(
        granted ? "ACCESO PERMITIDO" : "ACCESO DENEGADO",
        name,
        "Dias: " + String(daysLeft) + " | " + reason
    );

    if (granted) {
        playGrantedTone();
    } else {
        playDeniedTone();
    }
}

bool connectMqtt() {
    if (mqttClient.connected()) {
        return true;
    }

    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }

    mqttClient.setServer(MQTT_HOST, MQTT_PORT);
    mqttClient.setCallback(onMqttMessage);
    mqttClient.setBufferSize(512);

    const uint32_t chipId = static_cast<uint32_t>(ESP.getEfuseMac() & 0xFFFFFFFFULL);
    const String clientId = String(DEVICE_ID) + "-" + String(chipId, HEX);

    bool ok = false;
    if (strlen(MQTT_USER) > 0) {
        ok = mqttClient.connect(clientId.c_str(), MQTT_USER, MQTT_PASSWORD);
    } else {
        ok = mqttClient.connect(clientId.c_str());
    }

    if (ok) {
        mqttClient.subscribe(TOPIC_ACCESS_RESPONSE);
        drawStatus("MQTT conectado", TOPIC_ACCESS_RESPONSE);
        return true;
    }

    drawStatus("MQTT desconectado", "rc=" + String(mqttClient.state()));
    return false;
}

bool publishAccessRequest(
    int fingerprintId,
    const String& timestampIso,
    const String& deviceId
) {
    JsonDocument doc;
    doc["fingerprint_id"] = fingerprintId;
    doc["timestamp"] = timestampIso;
    doc["device_id"] = deviceId;

    String payload;
    serializeJson(doc, payload);

    if (mqttClient.connected()) {
        const bool sent = mqttClient.publish(TOPIC_ACCESS_REQUEST, payload.c_str());
        if (sent) {
            return true;
        }
    }

    return appendOfflineEvent(fingerprintId, timestampIso, deviceId);
}

void trySyncOfflineQueue() {
    if (WiFi.status() != WL_CONNECTED) {
        return;
    }

    const unsigned long now = millis();
    if (now - lastOfflineSyncAttemptMs < OFFLINE_SYNC_INTERVAL_MS) {
        return;
    }
    lastOfflineSyncAttemptMs = now;

    JsonDocument queueDoc;
    JsonArray queue = queueDoc.to<JsonArray>();
    const size_t queuedEvents = loadOfflineEvents(queue);
    if (queuedEvents == 0) {
        return;
    }

    HTTPClient http;
    const String endpoint = String(API_BASE_URL) + "/api/access/sync";
    http.begin(endpoint);
    http.addHeader("Content-Type", "application/json");

    String body;
    serializeJson(queue, body);
    const int status = http.POST(body);

    if (status == 200 || status == 201) {
        JsonDocument response;
        const String responseBody = http.getString();
        const DeserializationError err = deserializeJson(response, responseBody);

        const int processed = response["processed"] | 0;
        const int errors = response["errors"] | 0;

        if (!err && processed == static_cast<int>(queuedEvents) && errors == 0) {
            clearOfflineQueue();
            drawStatus("Sync offline OK", "Procesados: " + String(processed));
            playInfoTone();
        } else {
            drawStatus("Sync parcial/fallo", "HTTP " + String(status));
        }
    } else {
        drawStatus("Sync offline error", "HTTP " + String(status));
    }

    http.end();
}

void publishHeartbeat() {
    const unsigned long now = millis();
    if (now - lastHeartbeatMs < HEARTBEAT_INTERVAL_MS) {
        return;
    }

    lastHeartbeatMs = now;
    if (!mqttClient.connected()) {
        return;
    }

    JsonDocument heartbeat;
    heartbeat["device_id"] = DEVICE_ID;
    heartbeat["uptime"] = static_cast<uint32_t>(millis() / 1000);

    String payload;
    serializeJson(heartbeat, payload);
    mqttClient.publish(TOPIC_DEVICE_HEARTBEAT, payload.c_str());
}

int nextSimulatedFingerprint() {
    const int value = SIMULATED_FINGERPRINTS[simulatedFingerprintIndex];
    simulatedFingerprintIndex =
        (simulatedFingerprintIndex + 1) % SIMULATED_FINGERPRINTS_COUNT;
    return value;
}

void handleButtonPress() {
    const bool currentState = digitalRead(BUTTON_PIN);

    if (currentState != lastButtonState) {
        lastButtonEdgeMs = millis();
        lastButtonState = currentState;
    }

    const bool debouncedPress =
        currentState == LOW && (millis() - lastButtonEdgeMs) > BUTTON_DEBOUNCE_MS;
    if (!debouncedPress) {
        return;
    }

    while (digitalRead(BUTTON_PIN) == LOW) {
        delay(5);
    }

    const int fingerprintId = nextSimulatedFingerprint();
    const String timestampIso = isoTimestampNow();
    const bool delivered = publishAccessRequest(fingerprintId, timestampIso, DEVICE_ID);

    drawStatus(
        "Huella simulada",
        "fp=" + String(fingerprintId),
        delivered ? "enviada/queue ok" : "queue error"
    );

    if (delivered) {
        playInfoTone();
    } else {
        playDeniedTone();
    }

    lastButtonState = HIGH;
    lastButtonEdgeMs = millis();
}

void setupHardware() {
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    ledcSetup(BUZZER_CHANNEL, 2000, 8);
    ledcAttachPin(BUZZER_PIN, BUZZER_CHANNEL);
    ledcWriteTone(BUZZER_CHANNEL, 0);

    Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
    oled.begin(SSD1306_SWITCHCAPVCC, 0x3C);
    oled.clearDisplay();
    oled.display();
}

void setup() {
    Serial.begin(115200);
    delay(200);

    setupHardware();
    drawStatus("Taurus ESP32", "Iniciando...");

    if (!SPIFFS.begin(true)) {
        drawStatus("Error SPIFFS");
        playDeniedTone();
    }

    connectWiFi();
    connectMqtt();
    trySyncOfflineQueue();

    drawStatus("Listo", DEVICE_ID);
}

void loop() {
    const unsigned long now = millis();

    if (
        WiFi.status() != WL_CONNECTED &&
        now - lastWiFiReconnectAttemptMs > WIFI_RECONNECT_INTERVAL_MS
    ) {
        lastWiFiReconnectAttemptMs = now;
        connectWiFi();
    }

    if (
        !mqttClient.connected() &&
        now - lastMqttReconnectAttemptMs > MQTT_RECONNECT_INTERVAL_MS
    ) {
        lastMqttReconnectAttemptMs = now;
        connectMqtt();
    }

    mqttClient.loop();
    handleButtonPress();
    publishHeartbeat();
    trySyncOfflineQueue();

    delay(10);
}
