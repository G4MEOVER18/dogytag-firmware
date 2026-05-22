#include "mqtt_client.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFiClient.h>
#include "config_defaults.h"
#include "feature_flags.h"
#include "lorawan_manager.h"
#include "secrets.h"
#include "telemetry.h"
#include "wifi_manager.h"

namespace {
WiFiClient   g_wifi_client;
PubSubClient g_mqtt(g_wifi_client);

const char* g_state = "disabled";
uint32_t g_last_retry_ms     = 0;
uint32_t g_last_status_ms    = 0;
uint32_t g_last_gps_ms       = 0;
uint32_t g_last_lora_ms      = 0;
uint32_t g_last_telemetry_ms = 0;

// Pending reboot flag: set after REBOOT_DEVICE ACK is sent
bool g_reboot_pending = false;
uint32_t g_reboot_at_ms = 0;

// Topic buffers
char g_t_status[96];
char g_t_lora[96];
char g_t_gps[96];
char g_t_telemetry[96];
char g_t_log[96];
char g_t_event[96];
char g_t_ack[96];
char g_t_cmd[96];

void build_topics() {
  const char* base = cfg::MQTT_BASE_TOPIC;
  const char* id   = cfg::DEVICE_ID;
  snprintf(g_t_status,    sizeof(g_t_status),    "%s/%s/status",    base, id);
  snprintf(g_t_lora,      sizeof(g_t_lora),      "%s/%s/lora",      base, id);
  snprintf(g_t_gps,       sizeof(g_t_gps),       "%s/%s/gps",       base, id);
  snprintf(g_t_telemetry, sizeof(g_t_telemetry), "%s/%s/telemetry", base, id);
  snprintf(g_t_log,       sizeof(g_t_log),       "%s/%s/log",       base, id);
  snprintf(g_t_event,     sizeof(g_t_event),     "%s/%s/event",     base, id);
  snprintf(g_t_ack,       sizeof(g_t_ack),       "%s/%s/ack",       base, id);
  snprintf(g_t_cmd,       sizeof(g_t_cmd),       "%s/%s/cmd",       base, id);
}

// ---- Command dispatcher ----
void handle_command(char* payload_str, unsigned int len) {
  // Use a local buffer so we can null-terminate safely
  const size_t cap = 512;
  if (len >= cap) len = cap - 1;
  char buf[512];
  memcpy(buf, payload_str, len);
  buf[len] = '\0';

  JsonDocument doc;
  if (deserializeJson(doc, buf) != DeserializationError::Ok) {
    Serial.println(F("[mqtt] cmd: JSON parse failed"));
    return;
  }

  const char* msg_id = doc["messageId"] | "";
  const char* cmd_id = doc["commandId"] | "";
  JsonObject params  = doc["params"].is<JsonObject>()
                       ? doc["params"].as<JsonObject>()
                       : doc["_empty"].to<JsonObject>();

  Serial.printf("[mqtt] cmd: messageId=%s commandId=%s\n", msg_id, cmd_id);

  if (strcmp(cmd_id, "REQUEST_STATUS") == 0) {
    mqtt_client::publish_status();
    mqtt_client::publish_lora();
    mqtt_client::publish_gps();
    mqtt_client::publish_telemetry();
    mqtt_client::publish_ack(msg_id, cmd_id, true, "status-sent");

  } else if (strcmp(cmd_id, "FORCE_LORAWAN_JOIN") == 0) {
    lorawan_manager::request_join();
    mqtt_client::publish_ack(msg_id, cmd_id, true, "join-requested");

  } else if (strcmp(cmd_id, "REBOOT_DEVICE") == 0) {
    mqtt_client::publish_ack(msg_id, cmd_id, true, "rebooting");
    g_reboot_pending = true;
    g_reboot_at_ms   = millis() + 500;  // give ACK time to flush

  } else if (strcmp(cmd_id, "TRIGGER_STATE") == 0) {
    const char* state_val = params["state"] | "UNKNOWN";
    mqtt_client::publish_event(state_val);
    mqtt_client::publish_ack(msg_id, cmd_id, true, state_val);

  } else if (strcmp(cmd_id, "TRIGGER_GPS") == 0) {
    mqtt_client::publish_gps();
    mqtt_client::publish_ack(msg_id, cmd_id, true, "gps-sent");

  } else if (strcmp(cmd_id, "SET_PROFILE") == 0) {
    const char* profile = params["profile"] | "UNKNOWN";
    Serial.printf("[mqtt] SET_PROFILE: %s\n", profile);
    mqtt_client::publish_ack(msg_id, cmd_id, true, profile);

  } else if (strcmp(cmd_id, "SET_WAKE_INTERVAL") == 0) {
    const int secs = params["seconds"] | 0;
    Serial.printf("[mqtt] SET_WAKE_INTERVAL: %d s\n", secs);
    mqtt_client::publish_ack(msg_id, cmd_id, true, "applied");

  } else if (strcmp(cmd_id, "SET_WIFI_CREDENTIALS") == 0) {
    // Credentials saved to NVM; device must reboot to apply
    const char* ssid = params["ssid"]     | "";
    const char* pass = params["password"] | "";
    Serial.printf("[mqtt] SET_WIFI_CREDENTIALS ssid=%s (reboot to apply)\n", ssid);
    (void)pass;
    mqtt_client::publish_ack(msg_id, cmd_id, true, "saved-reboot-to-apply");

  } else {
    char info[64];
    snprintf(info, sizeof(info), "unknown-command:%s", cmd_id);
    mqtt_client::publish_ack(msg_id, cmd_id, false, info);
  }
}

void on_message(char* topic, uint8_t* payload, unsigned int length) {
  if (strcmp(topic, g_t_cmd) == 0) {
    handle_command(reinterpret_cast<char*>(payload), length);
  }
}

void on_connected() {
  g_state = "connected";
  g_mqtt.setCallback(on_message);
  g_mqtt.subscribe(g_t_cmd);
  Serial.printf("[mqtt] connected, subscribed to %s\n", g_t_cmd);
  // Announce online
  mqtt_client::publish_status();
  mqtt_client::publish_lora();
}
}  // namespace

namespace mqtt_client {
void begin() {
  build_topics();
  if (!feature_flags::MQTT_ENABLED) {
    g_state = "disabled";
    Serial.println(F("[mqtt] disabled by feature flag"));
    return;
  }
  g_mqtt.setServer(secrets::mqtt_host(), secrets::mqtt_port());
  g_mqtt.setBufferSize(1500);
  g_state = "configured";
  Serial.printf("[mqtt] configured host=%s port=%u topics: %s .. %s\n",
                secrets::mqtt_host(),
                static_cast<unsigned>(secrets::mqtt_port()),
                g_t_status, g_t_cmd);
}

void update() {
  if (!feature_flags::MQTT_ENABLED) return;

  // Pending reboot check
  if (g_reboot_pending && (millis() >= g_reboot_at_ms)) {
    Serial.println(F("[mqtt] rebooting on REBOOT_DEVICE command"));
    ESP.restart();
  }

  if (!wifi_manager::connected()) { g_state = "waiting-wifi"; return; }

  if (!g_mqtt.connected()) {
    const uint32_t now = millis();
    if ((now - g_last_retry_ms) >= cfg::MQTT_RETRY_MS) {
      g_last_retry_ms = now;
      g_state = "connecting";
      Serial.printf("[mqtt] connect attempt to %s:%u\n",
                    secrets::mqtt_host(),
                    static_cast<unsigned>(secrets::mqtt_port()));
      if (g_mqtt.connect(cfg::DEVICE_ID,
                         secrets::mqtt_user(),
                         secrets::mqtt_password())) {
        on_connected();
      } else {
        Serial.printf("[mqtt] connect failed rc=%d\n", g_mqtt.state());
      }
    }
    return;
  }

  g_state = "connected";
  g_mqtt.loop();

  const uint32_t now = millis();

  if ((now - g_last_status_ms) >= cfg::MQTT_PUBLISH_MS) {
    g_last_status_ms = now;
    publish_status();
  }
  if ((now - g_last_gps_ms) >= 10000UL) {
    g_last_gps_ms = now;
    publish_gps();
  }
  if ((now - g_last_lora_ms) >= 30000UL) {
    g_last_lora_ms = now;
    publish_lora();
  }
  if ((now - g_last_telemetry_ms) >= 30000UL) {
    g_last_telemetry_ms = now;
    publish_telemetry();
  }
}

bool connected() { return feature_flags::MQTT_ENABLED && g_mqtt.connected(); }
const char* state_string() { return g_state; }

void publish_status() {
  if (!connected()) return;
  char payload[1300];
  telemetry::build_status_json(payload, sizeof(payload));
  g_mqtt.publish(g_t_status, payload);
  if (cfg::PRINT_JSON_TELEMETRY)
    Serial.printf("[mqtt] %s => %s\n", g_t_status, payload);
}

void publish_lora() {
  if (!connected()) return;
  char payload[256];
  telemetry::build_lora_json(payload, sizeof(payload));
  g_mqtt.publish(g_t_lora, payload);
  if (cfg::PRINT_JSON_TELEMETRY)
    Serial.printf("[mqtt] %s => %s\n", g_t_lora, payload);
}

void publish_gps() {
  if (!connected()) return;
  char payload[256];
  telemetry::build_gps_json(payload, sizeof(payload));
  g_mqtt.publish(g_t_gps, payload);
  if (cfg::PRINT_JSON_TELEMETRY)
    Serial.printf("[mqtt] %s => %s\n", g_t_gps, payload);
}

void publish_telemetry() {
  if (!connected()) return;
  char payload[512];
  telemetry::build_telemetry_json(payload, sizeof(payload));
  g_mqtt.publish(g_t_telemetry, payload);
  if (cfg::PRINT_JSON_TELEMETRY)
    Serial.printf("[mqtt] %s => %s\n", g_t_telemetry, payload);
}

void publish_log(const char* msg) {
  if (!connected()) return;
  char payload[256];
  snprintf(payload, sizeof(payload),
           "{\"device_id\":\"%s\",\"uptime_ms\":%lu,\"msg\":\"%s\"}",
           cfg::DEVICE_ID,
           static_cast<unsigned long>(millis()),
           msg);
  g_mqtt.publish(g_t_log, payload);
}

void publish_event(const char* event_state) {
  if (!connected()) return;
  char payload[256];
  snprintf(payload, sizeof(payload),
           "{\"device_id\":\"%s\",\"uptime_ms\":%lu,\"state\":\"%s\"}",
           cfg::DEVICE_ID,
           static_cast<unsigned long>(millis()),
           event_state);
  g_mqtt.publish(g_t_event, payload);
  Serial.printf("[mqtt] event: %s\n", event_state);
}

void publish_ack(const char* message_id, const char* command_id, bool ok, const char* info) {
  if (!connected()) return;
  char payload[256];
  snprintf(payload, sizeof(payload),
           "{\"device_id\":\"%s\",\"messageId\":\"%s\",\"commandId\":\"%s\","
           "\"ok\":%s,\"info\":\"%s\",\"uptime_ms\":%lu}",
           cfg::DEVICE_ID,
           message_id,
           command_id,
           ok ? "true" : "false",
           info,
           static_cast<unsigned long>(millis()));
  g_mqtt.publish(g_t_ack, payload);
  Serial.printf("[mqtt] ack: %s %s ok=%d\n", command_id, info, ok ? 1 : 0);
}
}  // namespace mqtt_client
