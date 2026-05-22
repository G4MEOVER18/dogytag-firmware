#pragma once

#include <Arduino.h>

namespace runtime_config {
struct BeaconConfig {
  char text[65];
  bool loaded;
  bool dirty;
  bool join_autostart;
};

void begin();
BeaconConfig beacon_config();
const char* beacon_text();
size_t beacon_text_length();
bool set_beacon_text(const char* value);
void reset_beacon_text();
bool set_join_autostart(bool enabled);
bool join_autostart();
void reset_join_autostart();
bool save();
bool load();
size_t snapshot_json(char* buffer, size_t buffer_len);
const char* state_string();
}
