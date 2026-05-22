#pragma once
#include <Arduino.h>
namespace lorawan_manager {
struct RuntimeState {
  uint32_t join_attempts;
  uint32_t last_action_ms;
  bool radio_ready;
  bool secrets_ok;
  bool join_requested;
};
void begin();
void update();
const char* state_string();
const char* backend_string();
bool secrets_present();
bool join_requested();
void request_join();
uint32_t join_attempts();
RuntimeState runtime_state();
}
