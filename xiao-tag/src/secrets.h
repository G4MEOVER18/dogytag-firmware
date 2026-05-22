#pragma once
#if __has_include("../config/secrets.local.h")
#include "../config/secrets.local.h"
#define SMARTTAG_HAS_LOCAL_SECRETS 1
#else
#include "../config/secrets.example.h"
#define SMARTTAG_HAS_LOCAL_SECRETS 0
#endif
namespace secrets {
inline constexpr const char* wifi_ssid() { return WIFI_SSID; }
inline constexpr const char* wifi_password() { return WIFI_PASSWORD; }
inline constexpr const char* mqtt_host() { return MQTT_HOST; }
inline constexpr uint16_t mqtt_port() { return MQTT_PORT; }
inline constexpr const char* mqtt_user() { return MQTT_USER; }
inline constexpr const char* mqtt_password() { return MQTT_PASSWORD; }
inline constexpr const char* lorawan_dev_eui() { return LORAWAN_DEV_EUI; }
inline constexpr const char* lorawan_join_eui() { return LORAWAN_JOIN_EUI; }
inline constexpr const char* lorawan_app_key() { return LORAWAN_APP_KEY; }
}
