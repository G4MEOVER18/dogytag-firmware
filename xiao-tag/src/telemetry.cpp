#include "telemetry.h"

#include <ArduinoJson.h>

#include "build_info.h"
#include "config_defaults.h"
#include "gps.h"
#include "lorawan_backend.h"
#include "lorawan_backend_slot.h"
#include "lorawan_manager.h"
#include "lorawan_keys.h"
#include "mqtt_client.h"
#include "radio_sx1262.h"
#include "radio_sx1278.h"
#include "timebase.h"
#include "wifi_manager.h"
#include "runtime_config.h"

namespace telemetry {
void build_status_json(char* buffer, size_t buffer_len) {
  JsonDocument doc;
  char utc[48];
  timebase::utc_string(utc, sizeof(utc));
  const auto sx1262 = radio_sx1262::smoke_test_result();
  const auto sx1278 = radio_sx1278::smoke_test_result();
  const auto lorawan = lorawan_manager::runtime_state();
  const auto backend = lorawan_backend::runtime_info();
  const auto keys = lorawan_keys::validate_from_secrets();
  doc["v"] = APP_VERSION;
  doc["id"] = cfg::DEVICE_ID;
  doc["utc"] = utc;
  doc["pps"] = timebase::pps_count();
  doc["utc_synced"] = timebase::utc_synced();
  doc["gps_fix"] = gps::has_fix();
  doc["sats"] = gps::satellites();
  doc["wifi"] = wifi_manager::connected();
  doc["mqtt"] = mqtt_client::connected();
  doc["lorawan"] = lorawan_manager::state_string();
  doc["lorawan_backend"] = backend.backend_name;
  doc["lorawan_backend_state"] = backend.state;
  doc["lorawan_backend_initialized"] = backend.initialized;
  doc["lorawan_backend_join_supported"] = backend.join_supported;
  doc["lorawan_backend_library_available"] = backend.library_available;
  doc["lorawan_backend_real_allowed"] = backend.real_backend_allowed;
  doc["lorawan_backend_slot_header"] = backend.slot_header_available;
  doc["lorawan_backend_slot_object"] = backend.slot_object_scaffolded;
  doc["lorawan_backend_slot_state"] = backend.slot_state;
  doc["lorawan_backend_code"] = backend.readiness_code;
  doc["lorawan_backend_error"] = backend.error_code;
  doc["lorawan_keys_dev_ok"] = keys.dev_eui_ok;
  doc["lorawan_keys_join_ok"] = keys.join_eui_ok;
  doc["lorawan_keys_app_ok"] = keys.app_key_ok;
  doc["lorawan_backend_join_attempts"] = backend.join_attempts;
  doc["lorawan_join_attempts"] = lorawan.join_attempts;
  doc["lorawan_join_requested"] = lorawan.join_requested;
  doc["sx1262"] = radio_sx1262::state_string();
  doc["sx1262_status"] = sx1262.status_byte;
  doc["sx1262_errors"] = sx1262.error_word;
  doc["sx1262_standby_ok"] = sx1262.standby_cmd_ok;
  doc["sx1278"] = radio_sx1278::state_string();
  doc["sx1278_version"] = sx1278.version_reg;
  doc["sx1278_mode_ok"] = sx1278.mode_write_ok;
  doc["sx1278_tx_ready"] = sx1278.tx_scaffold_prepared;
  doc["sx1278_tx_mode_ok"] = sx1278.tx_mode_verified;
  doc["sx1278_tx_timeout"] = sx1278.tx_timeout_seen;
  doc["sx1278_tx_done"] = sx1278.tx_done_seen;
  doc["sx1278_tx_irq_flags"] = sx1278.tx_irq_flags;
  doc["sx1278_rx_mode_ok"] = sx1278.rx_mode_verified;
  doc["sx1278_rx_timeout"] = sx1278.rx_timeout_seen;
  doc["sx1278_rx_done"] = sx1278.rx_done_seen;
  doc["sx1278_rx_irq_flags"] = sx1278.rx_irq_flags;
  doc["sx1278_payload_len"] = sx1278.payload_length;
  doc["sx1278_payload_text"] = runtime_config::beacon_text();
  doc["runtime_cfg_state"] = runtime_config::state_string();
  doc["sx1278_tx_attempts"] = sx1278.tx_attempts;
  doc["sx1278_rx_attempts"] = sx1278.rx_attempts;
  serializeJson(doc, buffer, buffer_len);
}

void build_gps_json(char* buffer, size_t buffer_len) {
  JsonDocument doc;
  char utc[40];
  gps::format_utc(utc, sizeof(utc));
  doc["utc"] = utc;
  doc["fix"] = gps::has_fix();
  doc["lat"] = gps::latitude_deg();
  doc["lon"] = gps::longitude_deg();
  doc["sats"] = gps::satellites();
  doc["bytes"] = gps::bytes_seen();
  doc["age_ms"] = gps::last_fix_age_ms();
  serializeJson(doc, buffer, buffer_len);
}
void build_lora_json(char* buffer, size_t buffer_len) {
  JsonDocument doc;
  const auto lwb = lorawan_backend::runtime_info();
  doc["device_id"]    = cfg::DEVICE_ID;
  doc["joined"]       = lwb.joined;
  doc["join_attempts"]= lwb.join_attempts;
  doc["state"]        = lwb.state;
  doc["slot_state"]   = lwb.slot_state;
  doc["backend"]      = lwb.backend_name;
  doc["rssi"]         = lorawan_backend_slot::last_rssi();
  doc["snr"]          = lorawan_backend_slot::last_snr();
  serializeJson(doc, buffer, buffer_len);
}
void build_telemetry_json(char* buffer, size_t buffer_len) {
  // Compact per-cycle telemetry: uptime + GPS + LoRaWAN signal quality.
  // Distinct from /status (full diagnostics) and /lora (join state only).
  JsonDocument doc;
  char utc[40];
  gps::format_utc(utc, sizeof(utc));
  const auto lwb = lorawan_backend::runtime_info();
  doc["device_id"]   = cfg::DEVICE_ID;
  doc["uptime_ms"]   = static_cast<unsigned long>(millis());
  doc["utc"]         = utc;
  doc["gps_fix"]     = gps::has_fix();
  doc["lat"]         = gps::latitude_deg();
  doc["lon"]         = gps::longitude_deg();
  doc["sats"]        = gps::satellites();
  doc["gps_age_ms"]  = gps::last_fix_age_ms();
  doc["lorawan"]     = lwb.state;
  doc["joined"]      = lwb.joined;
  doc["rssi"]        = lorawan_backend_slot::last_rssi();
  doc["snr"]         = lorawan_backend_slot::last_snr();
  doc["wifi"]        = wifi_manager::connected();
  serializeJson(doc, buffer, buffer_len);
}
}  // namespace telemetry
