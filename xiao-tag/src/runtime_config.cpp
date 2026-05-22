#include "runtime_config.h"

#include <Arduino.h>
#include <Preferences.h>
#include <cstdio>
#include <cstring>

#include "config_defaults.h"
#include "feature_flags.h"

namespace {
Preferences g_prefs;
runtime_config::BeaconConfig g_cfg{};
const char* g_state = "boot";
constexpr const char* NS = "smarttag";
constexpr const char* KEY_BEACON = "beacon";
constexpr const char* KEY_AUTOJOIN = "autojoin";

void copy_text(const char* src) {
  std::strncpy(g_cfg.text, src, sizeof(g_cfg.text) - 1);
  g_cfg.text[sizeof(g_cfg.text) - 1] = '\0';
}
}  // namespace

namespace runtime_config {
void begin() {
  copy_text(cfg::SX1278_TEST_PAYLOAD);
  g_cfg.loaded = false;
  g_cfg.dirty = false;
  g_cfg.join_autostart = feature_flags::LORAWAN_AUTOSTART_JOIN_SCAFFOLD;
  load();
}

BeaconConfig beacon_config() { return g_cfg; }
const char* beacon_text() { return g_cfg.text; }
size_t beacon_text_length() { return std::strlen(g_cfg.text); }

bool set_beacon_text(const char* value) {
  if (value == nullptr || value[0] == '\0') return false;
  if (std::strlen(value) >= sizeof(g_cfg.text)) return false;
  copy_text(value);
  g_cfg.dirty = true;
  g_state = "modified";
  return true;
}

void reset_beacon_text() {
  copy_text(cfg::SX1278_TEST_PAYLOAD);
  g_cfg.dirty = true;
  g_state = "reset-default";
}

bool set_join_autostart(bool enabled) {
  g_cfg.join_autostart = enabled;
  g_cfg.dirty = true;
  g_state = enabled ? "autojoin-on" : "autojoin-off";
  return true;
}

bool join_autostart() { return g_cfg.join_autostart; }

void reset_join_autostart() {
  g_cfg.join_autostart = feature_flags::LORAWAN_AUTOSTART_JOIN_SCAFFOLD;
  g_cfg.dirty = true;
  g_state = "autojoin-reset-default";
}

bool save() {
  if (!g_prefs.begin(NS, false)) {
    g_state = "prefs-open-failed";
    return false;
  }
  const size_t beacon_written = g_prefs.putString(KEY_BEACON, String(g_cfg.text));
  const size_t autojoin_written = g_prefs.putBool(KEY_AUTOJOIN, g_cfg.join_autostart) ? 1u : 0u;
  g_prefs.end();
  if (beacon_written == 0 || autojoin_written == 0) {
    g_state = "save-failed";
    return false;
  }
  g_cfg.dirty = false;
  g_cfg.loaded = true;
  g_state = "saved";
  return true;
}

bool load() {
  if (!g_prefs.begin(NS, true)) {
    g_state = "prefs-open-failed";
    return false;
  }
  const String value = g_prefs.getString(KEY_BEACON, "");
  const bool autojoin = g_prefs.getBool(KEY_AUTOJOIN, feature_flags::LORAWAN_AUTOSTART_JOIN_SCAFFOLD);
  g_prefs.end();
  if (!value.isEmpty() && value.length() < static_cast<int>(sizeof(g_cfg.text))) {
    copy_text(value.c_str());
    g_cfg.loaded = true;
    g_cfg.dirty = false;
  } else {
    copy_text(cfg::SX1278_TEST_PAYLOAD);
  }
  g_cfg.join_autostart = autojoin;
  g_state = g_cfg.loaded ? "loaded" : "default";
  return g_cfg.loaded;
}

size_t snapshot_json(char* buffer, size_t buffer_len) {
  if (buffer == nullptr || buffer_len == 0) return 0;
  return static_cast<size_t>(std::snprintf(
      buffer,
      buffer_len,
      "{\"beacon\":\"%s\",\"beacon_len\":%u,\"loaded\":%d,\"dirty\":%d,\"join_autostart\":%d,\"state\":\"%s\"}",
      g_cfg.text,
      static_cast<unsigned>(std::strlen(g_cfg.text)),
      g_cfg.loaded ? 1 : 0,
      g_cfg.dirty ? 1 : 0,
      g_cfg.join_autostart ? 1 : 0,
      g_state));
}

const char* state_string() { return g_state; }
}  // namespace runtime_config
