#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <SPIFFS.h>
#include <Adafruit_Fingerprint.h>
#include <time.h>

#include "app_config.h"
#include "wifi_mqtt_config.h"

#ifdef USE_OLED
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#endif

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

HardwareSerial fingerprintSerial(2);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&fingerprintSerial);

#ifdef USE_OLED
Adafruit_SSD1306 oled(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET_PIN);
#endif

unsigned long lastWiFiReconnectAttemptMs = 0;
unsigned long lastMqttReconnectAttemptMs = 0;
unsigned long lastHeartbeatMs = 0;
unsigned long lastOfflineSyncAttemptMs = 0;
unsigned long lastFingerprintPollMs = 0;

bool fingerprintReady = false;
bool awaitingFingerRelease = false;

enum class EnrollStep {
    Idle,
    PlaceFinger,
    RemoveFinger,
    PlaceAgain,
    Building
};

struct EnrollSession {
    bool active = false;
    EnrollStep step = EnrollStep::Idle;
    int fingerprintId = -1;
    String memberId;
    unsigned long stepStartedAtMs = 0;
};

EnrollSession enrollSession;

void blinkLed(int pin, int onMs, int count = 1, int offMs = 100) {
    for (int i = 0; i < count; i++) {
        digitalWrite(pin, HIGH);
        delay(onMs);
        digitalWrite(pin, LOW);
        if (i < count - 1) delay(offMs);
    }
}

// ---- Estado: OLED real o LED provisional + Serial ----
#ifdef USE_OLED

String oledLine1, oledLine2, oledLine3;

void renderOled() {
    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);
    oled.setCursor(0, 0);
    oled.println(oledLine1);
    oled.println(oledLine2);
    oled.println(oledLine3);
    oled.display();
}

void drawStatus(
    const String& line1,
    const String& line2 = "",
    const String& line3 = ""
) {
    Serial.println("[STATUS] " + line1 + " | " + line2 + " | " + line3);
    oledLine1 = line1;
    oledLine2 = line2;
    oledLine3 = line3;
    renderOled();
}

// Si el OLED deja de responder por I2C (ruido, cables, caida de tension),
// lo reinicializa solo y redibuja el ultimo estado, sin reiniciar el ESP32.
void maintainOled() {
    static unsigned long lastOledCheckMs = 0;
    const unsigned long now = millis();
    if (now - lastOledCheckMs < OLED_HEALTHCHECK_INTERVAL_MS) {
        return;
    }
    lastOledCheckMs = now;

    Wire.beginTransmission(OLED_I2C_ADDR);
    if (Wire.endTransmission() == 0) {
        return;
    }

    if (oled.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
        renderOled();
    }
}

#else

void drawStatus(
    const String& line1,
    const String& line2 = "",
    const String& line3 = ""
) {
    Serial.println("[STATUS] " + line1 + " | " + line2 + " | " + line3);
    digitalWrite(LED_STATUS_PIN, HIGH);
    delay(150);
    digitalWrite(LED_STATUS_PIN, LOW);
}

void maintainOled() {}

#endif

// ---- Tonos: buzzer real o LEDs provisionales ----
#ifdef USE_BUZZER

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

#else

// Un solo LED de feedback: distintos patrones de parpadeo por evento.
void playGrantedTone() {
    blinkLed(LED_FEEDBACK_PIN, 90, 2, 40);   // doble parpadeo = acceso OK
}

void playDeniedTone() {
    blinkLed(LED_FEEDBACK_PIN, 220);         // un parpadeo largo = denegado/error
}

void playInfoTone() {
    blinkLed(LED_FEEDBACK_PIN, 70);          // un parpadeo corto = info
}

#endif

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
#ifndef USE_OLED
        digitalWrite(LED_WIFI_PIN, HIGH);
#endif
        playInfoTone();
    } else {
        drawStatus("WiFi sin conexion");
#ifndef USE_OLED
        digitalWrite(LED_WIFI_PIN, LOW);
#endif
    }
}

void publishEnrollProgress(
    const String& step,
    const String& status,
    const String& message
) {
    if (!mqttClient.connected()) {
        return;
    }

    JsonDocument doc;
    doc["device_id"] = DEVICE_ID;
    doc["member_id"] = enrollSession.memberId;
    doc["fingerprint_id"] = enrollSession.fingerprintId;
    doc["step"] = step;
    doc["status"] = status;
    doc["message"] = message;

    String payload;
    serializeJson(doc, payload);
    mqttClient.publish(TOPIC_ENROLL_RESPONSE, payload.c_str());
}

void resetEnrollSession() {
    enrollSession.active = false;
    enrollSession.step = EnrollStep::Idle;
    enrollSession.fingerprintId = -1;
    enrollSession.memberId = "";
    enrollSession.stepStartedAtMs = 0;
}

void startEnrollment(const String& memberId, int fingerprintId) {
    if (!fingerprintReady) {
        enrollSession.memberId = memberId;
        enrollSession.fingerprintId = fingerprintId;
        publishEnrollProgress("done", "failed", "AS608 no esta listo");
        resetEnrollSession();
        return;
    }

    if (fingerprintId < 1 || fingerprintId > 1000) {
        enrollSession.memberId = memberId;
        enrollSession.fingerprintId = fingerprintId;
        publishEnrollProgress("done", "failed", "fingerprint_id fuera de rango (1-1000)");
        resetEnrollSession();
        return;
    }

    enrollSession.active = true;
    enrollSession.memberId = memberId;
    enrollSession.fingerprintId = fingerprintId;
    enrollSession.step = EnrollStep::PlaceFinger;
    enrollSession.stepStartedAtMs = millis();

    drawStatus("Enrolando", "fp=" + String(fingerprintId), "Coloca el dedo");
    publishEnrollProgress("place_finger", "in_progress", "Coloca el dedo en el sensor");
    playInfoTone();
}

void deleteFingerprintSlot(int fingerprintId, const String& memberId) {
    if (!fingerprintReady) {
        enrollSession.memberId = memberId;
        enrollSession.fingerprintId = fingerprintId;
        publishEnrollProgress("delete", "failed", "AS608 no esta listo");
        resetEnrollSession();
        return;
    }

    enrollSession.memberId = memberId;
    enrollSession.fingerprintId = fingerprintId;

    if (fingerprintId == 0) {
        const uint8_t result = finger.emptyDatabase();
        publishEnrollProgress(
            "delete",
            result == FINGERPRINT_OK ? "success" : "failed",
            result == FINGERPRINT_OK ? "Base de huellas borrada" : "Error al borrar base"
        );
    } else {
        const uint8_t result = finger.deleteModel(static_cast<uint16_t>(fingerprintId));
        publishEnrollProgress(
            "delete",
            result == FINGERPRINT_OK ? "success" : "failed",
            result == FINGERPRINT_OK
                ? "Slot " + String(fingerprintId) + " borrado"
                : "Error al borrar slot " + String(fingerprintId)
        );
    }
    resetEnrollSession();
    drawStatus("Listo", DEVICE_ID);
}

void onMqttMessage(char* topic, byte* payload, unsigned int length) {
    String topicName(topic);
    String message;
    message.reserve(length);

    for (unsigned int i = 0; i < length; i++) {
        message += static_cast<char>(payload[i]);
    }

    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, message);

    if (topicName == TOPIC_ENROLL_REQUEST) {
        if (err) {
            return;
        }
        const String memberId = String(static_cast<const char*>(doc["member_id"] | ""));
        const int fingerprintId = doc["fingerprint_id"] | -1;
        startEnrollment(memberId, fingerprintId);
        return;
    }

    if (topicName == TOPIC_ENROLL_DELETE) {
        if (err) {
            return;
        }
        const String memberId = String(static_cast<const char*>(doc["member_id"] | ""));
        const int fingerprintId = doc["fingerprint_id"] | -1;
        deleteFingerprintSlot(fingerprintId, memberId);
        return;
    }

    if (topicName != TOPIC_ACCESS_RESPONSE) {
        return;
    }

    if (err) {
        drawStatus("MQTT resp invalida", err.c_str());
        playDeniedTone();
        return;
    }

    const bool granted = doc["granted"] | false;
    const String name = String(static_cast<const char*>(doc["name"] | ""));
    const int daysLeft = doc["days_left"] | 0;
    const String reason = String(static_cast<const char*>(doc["reason"] | ""));

    drawStatus(
        granted ? "ACCESO PERMITIDO" : "ACCESO DENEGADO",
        name,
        "Dias: " + String(daysLeft) + " | " + reason
    );

    if (granted) {
        playGrantedTone();
#ifndef USE_OLED
        digitalWrite(LED_STATUS_PIN, HIGH);
        delay(2000);
        digitalWrite(LED_STATUS_PIN, LOW);
#endif
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
        mqttClient.subscribe(TOPIC_ENROLL_REQUEST);
        mqttClient.subscribe(TOPIC_ENROLL_DELETE);
        drawStatus("MQTT conectado", TOPIC_ACCESS_RESPONSE);
#ifndef USE_OLED
        digitalWrite(LED_WIFI_PIN, HIGH);
#endif
        return true;
    }

    drawStatus("MQTT desconectado", "rc=" + String(mqttClient.state()));
#ifndef USE_OLED
    blinkLed(LED_WIFI_PIN, 100, 3, 100);
#endif
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

void handleFingerprintScan() {
    if (!fingerprintReady) {
        return;
    }

    if (enrollSession.active) {
        return;
    }

    const unsigned long now = millis();
    if (now - lastFingerprintPollMs < FINGERPRINT_POLL_INTERVAL_MS) {
        return;
    }
    lastFingerprintPollMs = now;

    const uint8_t imageStatus = finger.getImage();
    if (imageStatus == FINGERPRINT_NOFINGER) {
        awaitingFingerRelease = false;   // dedo retirado: listo para el siguiente
        return;
    }
    if (imageStatus != FINGERPRINT_OK) {
        return;
    }

    // Hay imagen. Si ya procesamos este contacto, espera a que retiren el dedo
    // antes de volver a evaluar (evita repetir el mensaje en bucle).
    if (awaitingFingerRelease) {
        return;
    }
    awaitingFingerRelease = true;

    int fingerprintId = -1;
    if (finger.image2Tz() == FINGERPRINT_OK &&
        finger.fingerSearch() == FINGERPRINT_OK) {
        fingerprintId = finger.fingerID;
    }

    if (fingerprintId < 0) {
        drawStatus("Huella no reconocida");
        playDeniedTone();
        return;
    }

    const String timestampIso = isoTimestampNow();
    const bool delivered = publishAccessRequest(fingerprintId, timestampIso, DEVICE_ID);

    drawStatus(
        "Huella detectada",
        "fp=" + String(fingerprintId),
        delivered ? "enviada/queue ok" : "queue error"
    );

    if (delivered) {
        playInfoTone();
    } else {
        playDeniedTone();
    }
}

void handleEnrollment() {
    if (!enrollSession.active) {
        return;
    }

    const unsigned long now = millis();
    if (now - enrollSession.stepStartedAtMs > ENROLL_FINGER_TIMEOUT_MS) {
        publishEnrollProgress("done", "failed", "Timeout esperando dedo");
        playDeniedTone();
        drawStatus("Enroll timeout", "fp=" + String(enrollSession.fingerprintId));
        resetEnrollSession();
        return;
    }

    if (now - lastFingerprintPollMs < FINGERPRINT_POLL_INTERVAL_MS) {
        return;
    }
    lastFingerprintPollMs = now;

    if (enrollSession.step == EnrollStep::PlaceFinger) {
        const uint8_t img = finger.getImage();
        if (img == FINGERPRINT_NOFINGER) {
            return;
        }
        if (img != FINGERPRINT_OK) {
            return;
        }
        if (finger.image2Tz(1) != FINGERPRINT_OK) {
            publishEnrollProgress("place_finger", "in_progress", "Imagen mala, intenta de nuevo");
            return;
        }
        publishEnrollProgress("remove_finger", "in_progress", "Quita el dedo");
        drawStatus("Enrolando", "fp=" + String(enrollSession.fingerprintId), "Quita el dedo");
        playInfoTone();
        enrollSession.step = EnrollStep::RemoveFinger;
        enrollSession.stepStartedAtMs = now;
        return;
    }

    if (enrollSession.step == EnrollStep::RemoveFinger) {
        const uint8_t img = finger.getImage();
        if (img == FINGERPRINT_NOFINGER) {
            publishEnrollProgress("place_again", "in_progress", "Coloca el dedo otra vez");
            drawStatus("Enrolando", "fp=" + String(enrollSession.fingerprintId), "Coloca otra vez");
            enrollSession.step = EnrollStep::PlaceAgain;
            enrollSession.stepStartedAtMs = now;
        }
        return;
    }

    if (enrollSession.step == EnrollStep::PlaceAgain) {
        const uint8_t img = finger.getImage();
        if (img == FINGERPRINT_NOFINGER) {
            return;
        }
        if (img != FINGERPRINT_OK) {
            return;
        }
        if (finger.image2Tz(2) != FINGERPRINT_OK) {
            publishEnrollProgress("place_again", "in_progress", "Imagen mala, intenta de nuevo");
            return;
        }
        enrollSession.step = EnrollStep::Building;
        enrollSession.stepStartedAtMs = now;
        return;
    }

    if (enrollSession.step == EnrollStep::Building) {
        if (finger.createModel() != FINGERPRINT_OK) {
            publishEnrollProgress("done", "failed", "Las dos huellas no coinciden");
            playDeniedTone();
            drawStatus("Enroll fallo", "huellas distintas");
            resetEnrollSession();
            return;
        }

        const uint8_t storeResult = finger.storeModel(static_cast<uint16_t>(enrollSession.fingerprintId));
        if (storeResult != FINGERPRINT_OK) {
            publishEnrollProgress("done", "failed", "Error guardando en flash");
            playDeniedTone();
            drawStatus("Enroll fallo", "store err");
            resetEnrollSession();
            return;
        }

        finger.getTemplateCount();
        publishEnrollProgress("done", "success", "Huella enrolada correctamente");
        drawStatus(
            "Enroll OK",
            "fp=" + String(enrollSession.fingerprintId),
            "Total: " + String(finger.templateCount)
        );
        playGrantedTone();
        resetEnrollSession();
    }
}

void setupFingerprint() {
    fingerprintSerial.begin(AS608_BAUD, SERIAL_8N1, AS608_RX_PIN, AS608_TX_PIN);
    finger.begin(AS608_BAUD);

    if (finger.verifyPassword()) {
        fingerprintReady = true;
        finger.getTemplateCount();
        drawStatus(
            "AS608 listo",
            "Plantillas: " + String(finger.templateCount)
        );
        playInfoTone();
    } else {
        fingerprintReady = false;
        drawStatus("AS608 no detectado", "Revisar UART/3.3V");
        playDeniedTone();
    }
}

void setupHardware() {
    // ---- Feedback sonoro ----
#ifdef USE_BUZZER
    ledcSetup(BUZZER_CHANNEL, 2000, 8);
    ledcAttachPin(BUZZER_PIN, BUZZER_CHANNEL);
    ledcWriteTone(BUZZER_CHANNEL, 0);
#else
    pinMode(LED_FEEDBACK_PIN, OUTPUT);
    digitalWrite(LED_FEEDBACK_PIN, LOW);
#endif

    // ---- Display de estado ----
#ifdef USE_OLED
    Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
    oled.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR);
    oled.clearDisplay();
    oled.display();
#else
    pinMode(LED_WIFI_PIN, OUTPUT);
    pinMode(LED_STATUS_PIN, OUTPUT);
    digitalWrite(LED_WIFI_PIN, LOW);
    digitalWrite(LED_STATUS_PIN, LOW);
#endif
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

    setupFingerprint();
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
    handleEnrollment();
    handleFingerprintScan();
    publishHeartbeat();
    trySyncOfflineQueue();
    maintainOled();

    delay(10);
}
