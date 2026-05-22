#pragma once
#include <Arduino.h>

namespace lorawan_backend_radiolib {
void begin();
void update();
bool can_start_join();
bool start_join();
int16_t send_uplink(uint8_t port, uint8_t* data, size_t len, bool confirmed = false);
const char* state_string();
unsigned long join_attempts();
bool initialized();
bool join_supported();
bool joined();
bool library_available();
bool real_backend_allowed();
bool real_backend_possible();
const char* readiness_code();
const char* error_code();
}  // namespace lorawan_backend_radiolib
