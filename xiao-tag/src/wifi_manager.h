#pragma once
namespace wifi_manager {
void begin();
void update();
bool connected();
const char* state_string();
}
