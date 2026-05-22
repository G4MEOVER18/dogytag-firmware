#include "lorawan_preflight.h"

#include <Arduino.h>
#include <cstdio>

#include "lorawan_backend.h"
#include "lorawan_keys.h"
#include "lorawan_manager.h"

namespace lorawan_preflight {
Report collect() {
  const auto keys = lorawan_keys::validate_from_secrets();
  const auto backend = lorawan_backend::runtime_info();
  const auto manager = lorawan_manager::runtime_state();

  Report r{};
  r.radio_ready = manager.radio_ready;
  r.dev_eui_ok = keys.dev_eui_ok;
  r.join_eui_ok = keys.join_eui_ok;
  r.app_key_ok = keys.app_key_ok;
  r.backend_initialized = backend.initialized;
  r.join_supported = backend.join_supported;
  r.join_requested = manager.join_requested;
  r.join_attempts = manager.join_attempts;
  r.backend_name = backend.backend_name;
  r.backend_state = backend.state;
  r.backend_slot_state = backend.slot_state;
  r.readiness_code = backend.readiness_code;
  r.error_code = backend.error_code;
  return r;
}

size_t to_json(const Report& report, char* buffer, size_t buffer_len) {
  if (buffer == nullptr || buffer_len == 0) {
    return 0;
  }
  return static_cast<size_t>(std::snprintf(
      buffer,
      buffer_len,
      "{\"radio_ready\":%d,\"keys\":{\"dev\":%d,\"join\":%d,\"app\":%d},\"backend\":\"%s\",\"backend_state\":\"%s\",\"backend_slot_state\":\"%s\",\"initialized\":%d,\"join_supported\":%d,\"requested\":%d,\"attempts\":%lu,\"code\":\"%s\",\"error\":\"%s\"}",
      report.radio_ready ? 1 : 0,
      report.dev_eui_ok ? 1 : 0,
      report.join_eui_ok ? 1 : 0,
      report.app_key_ok ? 1 : 0,
      report.backend_name,
      report.backend_state,
      report.backend_slot_state,
      report.backend_initialized ? 1 : 0,
      report.join_supported ? 1 : 0,
      report.join_requested ? 1 : 0,
      static_cast<unsigned long>(report.join_attempts),
      report.readiness_code,
      report.error_code));
}

void print_text(Stream& out, const Report& report) {
  out.printf("[preflight] radio=%s keys=%d/%d/%d backend=%s state=%s slot=%s init=%d join_supported=%d requested=%d joins=%lu code=%s err=%s\n",
             report.radio_ready ? "ready" : "not-ready",
             report.dev_eui_ok ? 1 : 0,
             report.join_eui_ok ? 1 : 0,
             report.app_key_ok ? 1 : 0,
             report.backend_name,
             report.backend_state,
             report.backend_slot_state,
             report.backend_initialized ? 1 : 0,
             report.join_supported ? 1 : 0,
             report.join_requested ? 1 : 0,
             static_cast<unsigned long>(report.join_attempts),
             report.readiness_code,
             report.error_code);
}
}  // namespace lorawan_preflight
