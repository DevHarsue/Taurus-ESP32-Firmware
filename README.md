# Taurus ESP32 Firmware

Firmware para el flujo de acceso del plan 25%:

- Conexion WiFi con reconexion.
- MQTT pub/sub contra Mosquitto.
- Boton fisico para simular huella.
- OLED para estado y resultado.
- Buzzer para feedback.
- Cola offline en SPIFFS.
- Heartbeat MQTT cada 2 minutos.
- Reintento de sync offline por HTTP (`/api/access/sync`).

## Requisitos

1. PlatformIO (extension VS Code o CLI).
2. Placa ESP32 (perfil `esp32dev`).

## Configuracion

Edita `include/wifi_mqtt_config.h`:

- `WIFI_SSID`, `WIFI_PASSWORD`
- `MQTT_HOST`, `MQTT_PORT`
- `API_BASE_URL` (ej. `http://192.168.1.10:8080`)
- `DEVICE_ID`

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
