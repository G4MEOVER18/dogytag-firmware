#pragma once
// =============================================================
// secrets.h – SmartTag HeltecLoRa32 V3
// TTN Application: dogytag-v1 | Device: heltec-lora32v3-tag
// EU868 | LoRaWAN 1.0.3 | OTAA
//
// IMPORTANT: Replace all REPLACE_* placeholders with your actual values.
// Never commit real keys to version control!
// =============================================================

// DevEUI als uint64_t (MSB-first) – dein Gerät aus TTN Console
#define LORAWAN_DEVEUI  0xREPLACE_DEV_EUI_UINT64ULL

// AppEUI / JoinEUI (TTN default: 0x0000000000000000)
#define LORAWAN_APPEUI  0x0000000000000000ULL

// AppKey (16 Bytes MSB-first) – aus TTN Console kopieren
#define LORAWAN_APPKEY  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
                          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }

// Raw-LoRa Tracker-ID (für NEAR_FIND Beacon, max 6 Zeichen)
#define TRACKER_ID  "ST-001"

// === WiFi (vorbereitet, noch nicht aktiv – WIFI_ENABLED 0) ===
// Anpassen wenn WiFi-Modus aktiviert wird:
#define WIFI_SSID  "REPLACE_WIFI_SSID"
#define WIFI_PASS  "REPLACE_WIFI_PASSWORD"

// === MQTT (vorbereitet, noch nicht aktiv) ===
#define MQTT_HOST  "REPLACE_MQTT_HOST"
#define MQTT_PORT  1883
#define MQTT_USER  "REPLACE_MQTT_USER"
#define MQTT_PASS  "REPLACE_MQTT_PASSWORD"

// === Device Identity (für spaeteres MQTT) ===
#define DEVICE_ID  "ST-001"
#define CLIENT_ID  "dogytag-ST-001"

// Firmware Version
#define FIRMWARE_VERSION  "v1.5.6"
