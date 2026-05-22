#pragma once

#include <Arduino.h>

namespace radio_sx1278 {
struct SmokeTestResult {
  bool executed;
  bool nss_ok;
  bool reset_pulsed;
  bool version_read_ok;
  bool mode_write_ok;
  bool tx_scaffold_prepared;
  bool tx_mode_entered;
  bool tx_mode_verified;
  bool tx_timeout_seen;
  bool tx_done_seen;
  bool rx_mode_entered;
  bool rx_mode_verified;
  bool rx_timeout_seen;
  bool rx_done_seen;
  uint8_t version_reg;
  uint8_t opmode_sleep;
  uint8_t opmode_standby;
  uint8_t opmode_tx;
  uint8_t opmode_rx;
  uint8_t payload_length;
  uint8_t tx_irq_flags;
  uint8_t rx_irq_flags;
  uint32_t tx_attempts;
  uint32_t rx_attempts;
};

void begin();
void update();
int dio0_level();
const char* state_string();
bool smoke_test_ok();
bool tx_scaffold_ready();
bool tx_test_completed();
bool rx_test_completed();
SmokeTestResult smoke_test_result();
uint8_t read_register(uint8_t reg);
void write_register(uint8_t reg, uint8_t value);
void request_test_beacon();
bool test_beacon_requested();
const char* current_payload_text();
}  // namespace radio_sx1278
