# Taurus ESP32 Firmware

Firmware para el flujo de acceso del plan 25%:

- Conexion WiFi con reconexion.
- MQTT pub/sub contra Mosquitto.
- Sensor de huella AS608 por UART2 (57600 bps).
- OLED SSD1306 (I2C) para estado y resultado, con auto-recuperacion si pierde el bus.
- Feedback por LED unico (o buzzer pasivo opcional).
- Cola offline en SPIFFS.
- Heartbeat MQTT cada 2 minutos.
- Reintento de sync offline por HTTP (`/api/access/sync`).

## Requisitos

1. PlatformIO (extension VS Code o CLI).
2. Placa ESP32 (perfil `esp32dev`).

## Hardware y conexiones

Estado actual del prototipo: **OLED conectado + LED de feedback** (el buzzer sigue
pendiente, emulado por el LED). Los toggles viven en `include/app_config.h`:

```c
#define USE_OLED      // OLED SSD1306 conectado
// #define USE_BUZZER // descomentar cuando el buzzer pasivo este conectado
```

Si un toggle esta comentado, ese periferico se emula con LED(s) provisional(es).

| Componente              | Pin componente | Pin ESP32 | Notas                         |
|-------------------------|----------------|-----------|-------------------------------|
| AS608 huella            | TX (amarillo)  | GPIO16    | UART2, 57600 bps              |
|                         | RX (verde)     | GPIO17    |                               |
|                         | VCC / GND      | 3V3 / GND | (o 5V/VIN si el USB es flojo) |
| OLED SSD1306            | SDA            | GPIO21    | I2C, direccion 0x3C           |
|                         | SCL            | GPIO22    |                               |
|                         | VCC / GND      | 3V3 / GND |                               |
| LED feedback (unico)    | anodo (+)      | GPIO25    | + resistencia 220 Ohm a GND   |

Pines **GPIO26, GPIO27, GPIO32 y GPIO33 quedan libres**. Al activar `USE_BUZZER`
el buzzer usa GPIO27.

## Configuracion

Las credenciales reales **no se versionan**. Copia la plantilla y rellena tus valores:

```bash
cp include/wifi_mqtt_config.local.h.example include/wifi_mqtt_config.local.h
```

Edita `include/wifi_mqtt_config.local.h` (este archivo está en `.gitignore`):

- `WIFI_SSID_VALUE`, `WIFI_PASSWORD_VALUE`
- `MQTT_HOST_VALUE`, `MQTT_PORT_VALUE`
- `API_BASE_URL_VALUE` (ej. `http://192.168.1.10:8080`)
- `DEVICE_ID_VALUE`

Si no creas el archivo local, `wifi_mqtt_config.h` usa placeholders (`CHANGE_ME`).

## Build / Upload

```bash
pio run
pio run -t upload
pio device monitor
```

## Contratos usados

- MQTT request: `gym/access/request`
- MQTT response: `gym/access/response`
- MQTT heartbeat: `gym/device/heartbeat`
- HTTP sync offline: `POST /api/access/sync`
