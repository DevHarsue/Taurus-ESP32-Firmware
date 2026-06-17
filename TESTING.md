# Testing — Taurus ESP32 Firmware

Guía paso a paso para flashear y probar el firmware con AS608 + LEDs (modo provisional) o AS608 + OLED + buzzer (modo original).

## 1. Hardware

### Sensor AS608 (común a ambos modelos)

El módulo AS608 que se está usando trae 4 cables (sin WAKEUP/touch):

```
AS608 VCC (rojo)         → ESP32 3.3V
AS608 GND (negro)        → ESP32 GND
AS608 TX  (amarillo)     → ESP32 GPIO 16 (RX2)
AS608 RX  (blanco/verde) → ESP32 GPIO 17 (TX2)
```

> El AS608 es un sensor óptico de huella con interfaz UART a 57600 bps. La librería usada es `Adafruit_Fingerprint`. Como no hay pin WAKEUP, el firmware sondea `finger.getImage()` en cada iteración del loop (cada 50 ms): si no hay dedo el sensor responde `FINGERPRINT_NOFINGER` y se descarta sin ruido.

### Modelo provisional (LEDs reemplazan OLED+buzzer)

```
ESP32 GPIO 25 ──[220Ω]──[Ánodo LED Verde   Cátodo]── GND   (Acceso concedido)
ESP32 GPIO 26 ──[220Ω]──[Ánodo LED Rojo    Cátodo]── GND   (Acceso denegado)
ESP32 GPIO 27 ──[220Ω]──[Ánodo LED Amarillo Cátodo]── GND   (Info / huella enviada)
ESP32 GPIO 32 ──[220Ω]──[Ánodo LED Azul    Cátodo]── GND   (Estado WiFi/MQTT)
ESP32 GPIO 33 ──[220Ω]──[Ánodo LED Blanco  Cátodo]── GND   (Status general)
```

> Pata **larga** del LED = ánodo (+). Pata corta = cátodo (−).

### Modelo original (OLED + buzzer + AS608)

Para usar el hardware completo, descomenta en `include/app_config.h`:

```cpp
#define USE_ORIGINAL_HARDWARE
```

Conexiones adicionales al AS608:

```
OLED VDD/VCC  → 3.3V
OLED GND      → GND
OLED SCK/SCL  → GPIO 22
OLED SDA      → GPIO 21
Buzzer (+)    → GPIO 27
Buzzer (-)    → GND
```

## 2. Configuración del firmware

Copia la plantilla y edita el archivo local (no versionado):

```bash
cp include/wifi_mqtt_config.local.h.example include/wifi_mqtt_config.local.h
```

```cpp
// include/wifi_mqtt_config.local.h
#define WIFI_SSID_VALUE      "TuRedWiFi"        // Red 2.4GHz (ESP32 no soporta 5GHz puro)
#define WIFI_PASSWORD_VALUE  "TuPassword"
#define MQTT_HOST_VALUE      "192.168.0.103"    // IP de la PC con Docker (ipconfig)
#define MQTT_PORT_VALUE      1883
#define API_BASE_URL_VALUE   "http://192.168.0.103:8080"
#define DEVICE_ID_VALUE      "esp32-recepcion"
```

Para encontrar la IP de tu PC en Windows:

```cmd
ipconfig
```

Busca el adaptador de WiFi/LAN → **Dirección IPv4**.

## 3. Backend Docker (GymTaurus)

El firmware espera el stack Docker corriendo en `C:\Users\Harsue\Desktop\CODIGOS\GymTaurus`.

### Levantar los servicios

```bash
cd C:\Users\Harsue\Desktop\CODIGOS\GymTaurus
docker compose up -d --build
```

### Verificar que están corriendo

```bash
docker compose ps
```

Debes ver al menos:

- `gymtaurus-mosquitto-1` con puerto `0.0.0.0:1883->1883/tcp`
- `gymtaurus-nginx-1` con puerto `0.0.0.0:8080->80/tcp`
- `gymtaurus-access-service-1` (Up)

### Verificar que el puerto 1883 escucha

```bash
netstat -an | findstr 1883
```

Debes ver `0.0.0.0:1883 LISTENING`.

### Firewall de Windows (si el ESP32 no conecta)

PowerShell como Administrador:

```powershell
New-NetFirewallRule -DisplayName "MQTT 1883" -Direction Inbound -LocalPort 1883 -Protocol TCP -Action Allow
New-NetFirewallRule -DisplayName "HTTP 8080" -Direction Inbound -LocalPort 8080 -Protocol TCP -Action Allow
```

## 4. Flashear el ESP32

```bash
cd C:\Users\Harsue\Desktop\CODIGOS\Taurus-ESP32-Firmware
pio run -t upload
pio device monitor -b 115200
```

O desde VSCode con PlatformIO:

1. Click en `Upload` (→) en la barra inferior
2. Click en `Serial Monitor` (🔌) para ver los logs

## 5. Comportamiento esperado

### Al arranque

| Momento | LED esperado | Serial output |
|---|---|---|
| Boot | LED blanco parpadea breve | `[STATUS] Taurus ESP32 \| Iniciando...` |
| AS608 OK | LED amarillo parpadea | `[STATUS] AS608 listo \| Plantillas: <N>` |
| AS608 falla | LED rojo se enciende | `[STATUS] AS608 no detectado \| Revisar UART/3.3V` |
| WiFi conectando | — | `[STATUS] WiFi conectando... \| <SSID>` |
| WiFi conectado | LED azul **fijo** + amarillo parpadea | `[STATUS] WiFi conectado \| <IP>` |
| MQTT conectado | LED azul sigue fijo | `[STATUS] MQTT conectado \| gym/access/response` |
| MQTT falla | LED azul parpadea 3× rápido | `[STATUS] MQTT desconectado \| rc=<n>` |

### Verificar conexión activa del ESP32

```bash
netstat -an | findstr 1883
```

Debes ver una línea como:

```
TCP    192.168.0.103:1883     192.168.0.XXX:NNNNN    ESTABLISHED
```

`192.168.0.XXX` es la IP de tu ESP32. Eso confirma que la conexión MQTT está viva.

## 6. Pruebas funcionales

### Test 1 — Escuchar todos los topics

Terminal 1 (mantenla abierta):

```bash
docker exec -it gymtaurus-mosquitto-1 mosquitto_sub -h localhost -t "gym/#" -v
```

Verás el heartbeat cada 2 minutos automáticamente:

```
gym/device/heartbeat {"device_id":"esp32-recepcion","uptime":120}
```

### Test 2 — Pasar el dedo por el AS608

Coloca un dedo previamente enrolado sobre la ventana óptica del AS608.

**Esperado:**
- El polling de `finger.getImage()` detecta la huella y llama `finger.fingerSearch()` para obtener el `fingerprint_id`
- LED amarillo parpadea
- En la terminal 1 aparece:
  ```
  gym/access/request {"fingerprint_id":<id>,"timestamp":"...","device_id":"esp32-recepcion"}
  ```
- En el monitor serial:
  ```
  [STATUS] Huella detectada | fp=<id> | enviada/queue ok
  ```

Si el dedo no está enrolado en el AS608, verás `[STATUS] Huella no reconocida` y LED rojo. Para enrolar nuevas plantillas usa el ejemplo `enroll.ino` de la librería Adafruit Fingerprint.

### Test 3 — Simular respuesta de acceso concedido

Terminal 2:

```bash
docker exec -it gymtaurus-mosquitto-1 mosquitto_pub -h localhost -t gym/access/response -m "{\"granted\":true,\"name\":\"Harsue\",\"days_left\":30,\"reason\":\"active\"}"
```

**Esperado:**
- LED verde parpadea 2 veces
- LED blanco se enciende fijo 2s
- Serial: `[STATUS] ACCESO PERMITIDO | Harsue | Dias: 30 | active`

### Test 4 — Simular respuesta de acceso denegado

```bash
docker exec -it gymtaurus-mosquitto-1 mosquitto_pub -h localhost -t gym/access/response -m "{\"granted\":false,\"name\":\"\",\"days_left\":0,\"reason\":\"expired\"}"
```

**Esperado:**
- LED rojo se enciende 220ms
- Serial: `[STATUS] ACCESO DENEGADO |  | Dias: 0 | expired`

### Test 5 — Flujo end-to-end con access-service

1. Pasa un dedo enrolado por el AS608 (dispara request real)
2. El `access-service` debería responder automáticamente si el `fingerprint_id` existe en MongoDB
3. Si no responde, revisa logs:
   ```bash
   docker logs gymtaurus-access-service-1 -f
   ```

### Test 6 — Cola offline (SPIFFS)

1. Apaga el broker: `docker stop gymtaurus-mosquitto-1`
2. Pasa un dedo 2-3 veces por el AS608 (los requests se encolan en SPIFFS)
3. Reinicia el broker: `docker start gymtaurus-mosquitto-1`
4. Espera ~30s — el ESP32 hace POST a `/api/access/sync` con la cola acumulada
5. Verás: `[STATUS] Sync offline OK | Procesados: N`

## 7. Troubleshooting

| Problema | Causa probable | Solución |
|---|---|---|
| Ningún LED enciende | Polaridad invertida | Pata larga del LED al GPIO |
| LEDs muy tenues | Resistencia muy alta | Cambiar 220Ω → 150Ω |
| `AS608 no detectado` | UART cruzado o baud incorrecto | Verificar AS608 TX→GPIO 16, AS608 RX→GPIO 17, baud 57600 |
| `AS608 no detectado` | Alimentación insuficiente | Confirmar 3.3V estables (algunos módulos toleran 5V; revisar datasheet) |
| `Huella no reconocida` | Plantilla no enrolada | Enrolar la huella con el ejemplo `enroll.ino` de Adafruit Fingerprint |
| Sensor lee aleatoriamente | Polling muy agresivo | Subir `FINGERPRINT_POLL_INTERVAL_MS` en `app_config.h` |
| WiFi no conecta | Red 5GHz | Usar red 2.4GHz |
| MQTT no conecta | Puerto 1883 no expuesto | Verificar `docker compose ps` muestra `0.0.0.0:1883->1883/tcp` |
| MQTT no conecta | Firewall Windows | Crear regla inbound TCP 1883 |
| MQTT no conecta | IP incorrecta | Verificar `ipconfig` y actualizar `MQTT_HOST` |
| Compilación falla por OLED | Macros mezcladas | Asegurar que `USE_ORIGINAL_HARDWARE` está consistente |
| ESP32 no aparece en `pio device list` | Driver USB-UART | Instalar driver CH340 o CP2102 según el chip |

## 8. Pasar al hardware original

Cuando tengas la pantalla OLED y el buzzer (el AS608 es el mismo en ambos modos):

1. Descomenta `#define USE_ORIGINAL_HARDWARE` en `include/app_config.h`
2. Conecta el OLED y el buzzer según la sección 1 (el AS608 sigue conectado igual)
3. Re-flashea: `pio run -t upload`

El firmware automáticamente desactiva los LEDs y activa OLED + buzzer.
