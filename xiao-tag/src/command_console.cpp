#include "command_console.h"

#include <Arduino.h>
#include <cstring>

#include "build_info.h"
#include "lorawan_backend.h"
#include "lorawan_manager.h"
#include "lorawan_preflight.h"
#include "radio_sx1278.h"
#include "runtime_config.h"
#include "telemetry.h"

namespace {
char g_line[128]{};
size_t g_len = 0;
const char* g_state = "idle";

void print_help() {
  Serial.println(F("[console] commands: help | version | uptime | console | status | backend | backendjson | preflight | preflightjson | keys | join | beacon | beacon get | beacon set <text> | beacon save | beacon reset | autojoin get | autojoin on | autojoin off | autojoin save | autojoin reset | cfgjson | gpsjson | rawjson"));
}

void handle_line(const char* line) {
  if (std::strcmp(line, "help") == 0) {
    print_help();
    g_state = "help";
    return;
  }
  if (std::strcmp(line, "version") == 0) {
    Serial.printf("[console] firmware=%s build=%s %s\n", APP_VERSION, BUILD_DATE, BUILD_TIME);
    g_state = "version";
    return;
  }
  if (std::strcmp(line, "uptime") == 0) {
    Serial.printf("[console] uptime_ms=%lu\n", static_cast<unsigned long>(millis()));
    g_state = "uptime";
    return;
  }
  if (std::strcmp(line, "console") == 0) {
    Serial.printf("[console] state=%s line_buffer=%u\n", g_state, static_cast<unsigned>(g_len));
    g_state = "console";
    return;
  }
  if (std::strcmp(line, "status") == 0) {
    Serial.printf("[console] lorawan=%s joins=%lu sx1278=%s\n",
                  lorawan_manager::state_string(),
                  static_cast<unsigned long>(lorawan_manager::join_attempts()),
                  radio_sx1278::state_string());
    g_state = "status";
    return;
  }
  if (std::strcmp(line, "backend") == 0) {
    const auto backend = lorawan_backend::runtime_info();
    Serial.printf("[console] backend=%s state=%s slot=%s code=%s err=%s lib=%d real=%d header=%d scaffold=%d\n",
                  backend.backend_name,
                  backend.state,
                  backend.slot_state,
                  backend.readiness_code,
                  backend.error_code,
                  backend.library_available ? 1 : 0,
                  backend.real_backend_allowed ? 1 : 0,
                  backend.slot_header_available ? 1 : 0,
                  backend.slot_object_scaffolded ? 1 : 0);
    g_state = "backend";
    return;
  }
  if (std::strcmp(line, "backendjson") == 0) {
    const auto backend = lorawan_backend::runtime_info();
    char payload[384];
    std::snprintf(payload,
                  sizeof(payload),
                  "{\"backend\":\"%s\",\"state\":\"%s\",\"slot\":\"%s\",\"code\":\"%s\",\"error\":\"%s\",\"lib\":%d,\"real\":%d,\"header\":%d,\"scaffold\":%d,\"attempts\":%lu}",
                  backend.backend_name,
                  backend.state,
                  backend.slot_state,
                  backend.readiness_code,
                  backend.error_code,
                  backend.library_available ? 1 : 0,
                  backend.real_backend_allowed ? 1 : 0,
                  backend.slot_header_available ? 1 : 0,
                  backend.slot_object_scaffolded ? 1 : 0,
                  static_cast<unsigned long>(backend.join_attempts));
    Serial.printf("[console] %s\n", payload);
    g_state = "backendjson";
    return;
  }
  if (std::strcmp(line, "preflight") == 0) {
    lorawan_preflight::print_text(Serial, lorawan_preflight::collect());
    g_state = "preflight";
    return;
  }
  if (std::strcmp(line, "preflightjson") == 0) {
    char payload[512];
    lorawan_preflight::to_json(lorawan_preflight::collect(), payload, sizeof(payload));
    Serial.printf("[console] %s\n", payload);
    g_state = "preflightjson";
    return;
  }
  if (std::strcmp(line, "keys") == 0) {
    const auto report = lorawan_preflight::collect();
    Serial.printf("[console] keys dev=%d join=%d app=%d\n",
                  report.dev_eui_ok ? 1 : 0,
                  report.join_eui_ok ? 1 : 0,
                  report.app_key_ok ? 1 : 0);
    g_state = "keys";
    return;
  }
  if (std::strcmp(line, "join") == 0) {
    lorawan_manager::request_join();
    Serial.println(F("[console] join requested"));
    g_state = "join";
    return;
  }
  if (std::strcmp(line, "beacon") == 0) {
    radio_sx1278::request_test_beacon();
    Serial.printf("[console] sx1278 beacon requested payload=\"%s\"\n", radio_sx1278::current_payload_text());
    g_state = "beacon";
    return;
  }
  if (std::strcmp(line, "beacon get") == 0) {
    Serial.printf("[console] beacon text=\"%s\" len=%u state=%s\n",
                  runtime_config::beacon_text(),
                  static_cast<unsigned>(runtime_config::beacon_text_length()),
                  runtime_config::state_string());
    g_state = "beacon-get";
    return;
  }
  if (std::strncmp(line, "beacon set ", 11) == 0) {
    const char* value = line + 11;
    const bool ok = runtime_config::set_beacon_text(value);
    Serial.printf("[console] beacon set ok=%d text=\"%s\"\n", ok ? 1 : 0, runtime_config::beacon_text());
    g_state = ok ? "beacon-set" : "beacon-set-failed";
    return;
  }
  if (std::strcmp(line, "beacon save") == 0) {
    const bool ok = runtime_config::save();
    Serial.printf("[console] beacon save ok=%d state=%s\n", ok ? 1 : 0, runtime_config::state_string());
    g_state = ok ? "beacon-save" : "beacon-save-failed";
    return;
  }
  if (std::strcmp(line, "beacon reset") == 0) {
    runtime_config::reset_beacon_text();
    Serial.printf("[console] beacon reset text=\"%s\"\n", runtime_config::beacon_text());
    g_state = "beacon-reset";
    return;
  }
  if (std::strcmp(line, "autojoin get") == 0) {
    Serial.printf("[console] autojoin=%d state=%s\n", runtime_config::join_autostart() ? 1 : 0, runtime_config::state_string());
    g_state = "autojoin-get";
    return;
  }
  if (std::strcmp(line, "autojoin on") == 0) {
    runtime_config::set_join_autostart(true);
    Serial.printf("[console] autojoin=%d\n", runtime_config::join_autostart() ? 1 : 0);
    g_state = "autojoin-on";
    return;
  }
  if (std::strcmp(line, "autojoin off") == 0) {
    runtime_config::set_join_autostart(false);
    Serial.printf("[console] autojoin=%d\n", runtime_config::join_autostart() ? 1 : 0);
    g_state = "autojoin-off";
    return;
  }
  if (std::strcmp(line, "autojoin save") == 0) {
    const bool ok = runtime_config::save();
    Serial.printf("[console] autojoin save ok=%d state=%s\n", ok ? 1 : 0, runtime_config::state_string());
    g_state = ok ? "autojoin-save" : "autojoin-save-failed";
    return;
  }
  if (std::strcmp(line, "autojoin reset") == 0) {
    runtime_config::reset_join_autostart();
    Serial.printf("[console] autojoin reset value=%d\n", runtime_config::join_autostart() ? 1 : 0);
    g_state = "autojoin-reset";
    return;
  }
  if (std::strcmp(line, "cfgjson") == 0) {
    char payload[256];
    runtime_config::snapshot_json(payload, sizeof(payload));
    Serial.printf("[console] %s\n", payload);
    g_state = "cfgjson";
    return;
  }
  if (std::strcmp(line, "gpsjson") == 0) {
    char payload[512];
    telemetry::build_gps_json(payload, sizeof(payload));
    Serial.printf("[console] %s\n", payload);
    g_state = "gpsjson";
    return;
  }
  if (std::strcmp(line, "rawjson") == 0) {
    char payload[1300];
    telemetry::build_status_json(payload, sizeof(payload));
    Serial.printf("[console] %s\n", payload);
    g_state = "rawjson";
    return;
  }
  Serial.printf("[console] unknown command: %s\n", line);
  g_state = "unknown";
}
}  // namespace

namespace command_console {
void begin() {
  g_len = 0;
  g_line[0] = '\0';
  Serial.println(F("[console] ready - type 'help'"));
}

void update() {
  while (Serial.available() > 0) {
    const int ch = Serial.read();
    if (ch == '\r') continue;
    if (ch == '\n') {
      g_line[g_len] = '\0';
      if (g_len > 0) handle_line(g_line);
      g_len = 0;
      g_line[0] = '\0';
      continue;
    }
    if (g_len + 1 < sizeof(g_line)) {
      g_line[g_len++] = static_cast<char>(ch);
      g_line[g_len] = '\0';
    }
  }
}

const char* state_string() { return g_state; }
}  // namespace command_console
