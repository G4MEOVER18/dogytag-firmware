#pragma once

#include <Arduino.h>

namespace lorawan_preflight {
struct Report {
  bool radio_ready;
  bool dev_eui_ok;
  bool join_eui_ok;
  bool app_key_ok;
  bool backend_initialized;
  bool join_supported;
  bool join_requested;
  uint32_t join_attempts;
  const char* backend_name;
  const char* backend_state;
  const char* backend_slot_state;
  const char* readiness_code;
  const char* error_code;
};

Report collect();
size_t to_json(const Report& report, char* buffer, size_t buffer_len);
void print_text(Stream& out, const Report& report);
}  // namespace lorawan_preflight
