#pragma once

#include <Arduino.h>

namespace lorawan_backend {
struct RuntimeInfo {
  bool configured;
  bool initialized;
  bool join_supported;
  bool join_started;
  bool joined;
  bool library_available;
  bool real_backend_allowed;
  bool slot_header_available;
  bool slot_object_scaffolded;
  uint32_t join_attempts;
  const char* backend_name;
  const char* state;
  const char* slot_state;
  const char* readiness_code;
  const char* error_code;
};

void begin();
void update();
bool can_start_join();
bool start_join();
int16_t send_uplink(uint8_t port, uint8_t* data, size_t len, bool confirmed = false);
RuntimeInfo runtime_info();
const char* backend_name();
const char* state_string();
}  // namespace lorawan_backend
