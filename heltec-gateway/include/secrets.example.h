#pragma once
// =============================================================
// secrets.h – SmartTag HeltecTracker v1.2 Gateway
// Role: LoRa NEAR_FIND Receiver + MQTT Bridge
// TTN App: dogytag-v1 | Device: gateway01
//
// Architektur:
//   Gateway → MQTT "REPLACE_MQTT_HOST" → lokale Bridge → TTN MQTT
// =============================================================

// === WiFi ===
// ANPASSEN: SSID und Passwort deines Heimnetzwerks
#define WIFI_SSID  "REPLACE_WIFI_SSID"
#define WIFI_PASS  "REPLACE_WIFI_PASSWORD"

// === MQTT Broker (BreachGateway VM – lokaler Hauptbroker) ===
#define MQTT_HOST  "REPLACE_MQTT_HOST"
#define MQTT_PORT  1883
#define MQTT_USER  "REPLACE_MQTT_USER"
#define MQTT_PASS  "REPLACE_MQTT_PASSWORD"

// === TTN App ===
#define TTN_APP_ID  "dogytag-v1@ttn"

// === Device Identity ===
#define DEVICE_ID  "gateway01"
#define CLIENT_ID  "dogytag-gateway01"

// === Gateway LoRa Test-TX ID ===
#define GW_LORA_ID  "GW-001"

// === Firmware Version ===
#define FIRMWARE_VERSION  "v3.0.0"
