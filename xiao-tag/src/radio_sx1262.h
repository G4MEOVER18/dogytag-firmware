#pragma once
#include <Arduino.h>
namespace radio_sx1262 {
struct SmokeTestResult {
  bool executed;
  bool reset_pulsed;
  bool busy_seen;
  bool status_read_ok;
  bool error_read_ok;
  bool standby_cmd_ok;
  uint8_t status_byte;
  uint16_t error_word;
};
void begin();
void update();
int busy_level();
int dio1_level();
const char* state_string();
bool looks_alive();
SmokeTestResult smoke_test_result();
uint8_t command_read1(uint8_t opcode);
uint16_t command_read2(uint8_t opcode);
bool command_write1(uint8_t opcode, uint8_t value);
}
