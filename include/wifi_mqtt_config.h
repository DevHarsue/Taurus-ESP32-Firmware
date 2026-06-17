#pragma once

// ===========================================================================
//  Configuración de red / MQTT
// ---------------------------------------------------------------------------
//  NO pongas credenciales reales en este archivo: se versiona en git.
//  Copia "wifi_mqtt_config.local.h.example" a "wifi_mqtt_config.local.h"
//  y escribe ahí tus valores reales. Ese archivo está en .gitignore y nunca
//  se sube al repositorio. Si existe, sobreescribe los placeholders de abajo.
// ===========================================================================

#if __has_include("wifi_mqtt_config.local.h")
#include "wifi_mqtt_config.local.h"
#endif

// ---- Valores por defecto (placeholders) -----------------------------------
// Solo se aplican si el header local no los define.
#ifndef WIFI_SSID_VALUE
#define WIFI_SSID_VALUE "CHANGE_ME"
#endif
#ifndef WIFI_PASSWORD_VALUE
#define WIFI_PASSWORD_VALUE "CHANGE_ME"
#endif
#ifndef MQTT_HOST_VALUE
#define MQTT_HOST_VALUE "127.0.0.1"
#endif
#ifndef MQTT_PORT_VALUE
#define MQTT_PORT_VALUE 1883
#endif
#ifndef MQTT_USER_VALUE
#define MQTT_USER_VALUE ""
#endif
#ifndef MQTT_PASSWORD_VALUE
#define MQTT_PASSWORD_VALUE ""
#endif
#ifndef API_BASE_URL_VALUE
#define API_BASE_URL_VALUE "http://127.0.0.1:8080"
#endif
#ifndef DEVICE_ID_VALUE
#define DEVICE_ID_VALUE "esp32-recepcion"
#endif

// WiFi
constexpr const char* WIFI_SSID = WIFI_SSID_VALUE;
constexpr const char* WIFI_PASSWORD = WIFI_PASSWORD_VALUE;

// MQTT (Mosquitto)
constexpr const char* MQTT_HOST = MQTT_HOST_VALUE;
constexpr uint16_t MQTT_PORT = MQTT_PORT_VALUE;
constexpr const char* MQTT_USER = MQTT_USER_VALUE;
constexpr const char* MQTT_PASSWORD = MQTT_PASSWORD_VALUE;

// HTTP endpoint for offline sync
constexpr const char* API_BASE_URL = API_BASE_URL_VALUE;

// Device identity
constexpr const char* DEVICE_ID = DEVICE_ID_VALUE;

// NTP
constexpr const char* NTP_SERVER_1 = "pool.ntp.org";
constexpr const char* NTP_SERVER_2 = "time.nist.gov";
