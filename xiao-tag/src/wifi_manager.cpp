#include "wifi_manager.h"
#include <Arduino.h>
#include <WiFi.h>
#include "config_defaults.h"
#include "feature_flags.h"
#include "secrets.h"

namespace {
uint32_t g_last_retry_ms = 0;
const char* g_state = "disabled";
}

namespace wifi_manager {
void begin() {
  if (!feature_flags::WIFI_ENABLED) {
    g_state = "disabled";
    Serial.println(F("[wifi] disabled by feature flag"));
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(secrets::wifi_ssid(), secrets::wifi_password());
  g_last_retry_ms = millis();
  g_state = "connecting";
  Serial.printf("[wifi] connecting to SSID '%s'\n", secrets::wifi_ssid());
}
void update() {
  if (!feature_flags::WIFI_ENABLED) return;
  const wl_status_t status = WiFi.status();
  if (status == WL_CONNECTED) {
    g_state = "connected";
    return;
  }
  // setAutoReconnect(true) handles reconnection to the same AP automatically.
  // Manual kick only if we've been disconnected long enough, to recover from
  // states like WL_CONNECT_FAILED where autoReconnect doesn't retry.
  const uint32_t now = millis();
  if (status == WL_CONNECT_FAILED || status == WL_NO_SSID_AVAIL) {
    g_state = "connect-failed";
    if ((now - g_last_retry_ms) >= cfg::WIFI_RETRY_MS) {
      g_last_retry_ms = now;
      Serial.printf("[wifi] retry after failure (status=%d)\n", (int)status);
      WiFi.disconnect(false);
      WiFi.begin(secrets::wifi_ssid(), secrets::wifi_password());
    }
  } else {
    g_state = "connecting";
  }
}
bool connected() { return feature_flags::WIFI_ENABLED && (WiFi.status() == WL_CONNECTED); }
const char* state_string() { return g_state; }
}
