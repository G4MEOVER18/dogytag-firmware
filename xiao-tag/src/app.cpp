#include "app.h"
#include <Arduino.h>
#include "config_defaults.h"
#include "lorawan_backend.h"
#include "mqtt_client.h"
#include "telemetry.h"

namespace {
uint32_t g_last_heartbeat_ms = 0;
uint32_t g_last_uplink_ms    = 0;
bool     g_was_joined        = false;

// Interval between LoRaWAN uplinks after join (5 minutes)
static constexpr uint32_t UPLINK_INTERVAL_MS = 5UL * 60UL * 1000UL;

void send_status_uplink() {
  // Compact 4-byte uplink: 0x01 (type), uptime_seconds (3 bytes BE)
  const uint32_t uptime_s = millis() / 1000UL;
  uint8_t payload[4] = {
    0x01,
    static_cast<uint8_t>((uptime_s >> 16) & 0xFF),
    static_cast<uint8_t>((uptime_s >>  8) & 0xFF),
    static_cast<uint8_t>( uptime_s        & 0xFF)
  };
  const int16_t rc = lorawan_backend::send_uplink(1, payload, sizeof(payload));
  char msg[64];
  snprintf(msg, sizeof(msg), "uplink port=1 rc=%d", (int)rc);
  Serial.printf("[app] %s\n", msg);
  mqtt_client::publish_log(msg);
  mqtt_client::publish_lora();  // refresh RSSI/SNR after uplink
}
}  // namespace

namespace app {
void begin() {
  Serial.println(F("[app] begin"));
}

void update() {
  const uint32_t now = millis();

  // Heartbeat
  if ((now - g_last_heartbeat_ms) >= cfg::APP_HEARTBEAT_MS) {
    g_last_heartbeat_ms = now;
    if (cfg::PRINT_JSON_TELEMETRY) {
      char payload[512];
      telemetry::build_status_json(payload, sizeof(payload));
      Serial.printf("[app] heartbeat %s\n", payload);
    } else {
      Serial.println(F("[app] heartbeat"));
    }
  }

  // Detect LoRaWAN join transitions
  const bool joined = lorawan_backend::runtime_info().joined;
  if (joined && !g_was_joined) {
    mqtt_client::publish_lora();
    mqtt_client::publish_event("LORAWAN_JOINED");
    mqtt_client::publish_log("LoRaWAN OTAA join successful");
    g_last_uplink_ms = now - UPLINK_INTERVAL_MS;  // trigger first uplink immediately
  } else if (!joined && g_was_joined) {
    mqtt_client::publish_event("LORAWAN_LOST");
    mqtt_client::publish_log("LoRaWAN connection lost");
  }
  g_was_joined = joined;

  // Periodic uplink when joined
  if (joined && (now - g_last_uplink_ms) >= UPLINK_INTERVAL_MS) {
    g_last_uplink_ms = now;
    send_status_uplink();
  }
}
}  // namespace app
