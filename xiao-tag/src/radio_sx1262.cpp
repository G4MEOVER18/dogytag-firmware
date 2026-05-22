#include "radio_sx1262.h"
#include <Arduino.h>
#include <SPI.h>
#include "config_defaults.h"
#include "feature_flags.h"
#include "pins.h"

namespace {
const char* g_state = "disabled";
radio_sx1262::SmokeTestResult g_smoke{};
bool g_smoke_done = false;
uint32_t g_begin_ms = 0;

inline void chip_select(bool active) { digitalWrite(PIN_SX1262_NSS, active ? LOW : HIGH); }
void pulse_reset() { digitalWrite(PIN_SX1262_RST, LOW); delay(2); digitalWrite(PIN_SX1262_RST, HIGH); delay(20); }
bool wait_while_busy(uint32_t timeout_ms) {
  const uint32_t start = millis();
  while (digitalRead(PIN_SX1262_BUSY) == HIGH) {
    if ((millis() - start) >= timeout_ms) return false;
    delay(1);
  }
  return true;
}
void tx_opcode(uint8_t opcode) { SPI.transfer(opcode); }
}

namespace radio_sx1262 {
void begin() {
  if (!feature_flags::SX1262_ENABLED) {
    g_state = "disabled";
    Serial.println(F("[sx1262] disabled by feature flag"));
    return;
  }
  pinMode(PIN_SX1262_NSS, OUTPUT);
  chip_select(false);
  pinMode(PIN_SX1262_RST, OUTPUT);
  digitalWrite(PIN_SX1262_RST, HIGH);
  pinMode(PIN_SX1262_BUSY, INPUT);
  pinMode(PIN_SX1262_DIO1, INPUT);
  pinMode(PIN_SX1262_RF_SW, OUTPUT);
  digitalWrite(PIN_SX1262_RF_SW, LOW);
  pulse_reset();
  g_smoke.reset_pulsed = true;
  g_begin_ms = millis();
  g_state = feature_flags::SX1262_SMOKE_TEST_ENABLED ? "smoke-pending" : "ready";
  Serial.println(F("[sx1262] initialized"));
}

void update() {
  if (!feature_flags::SX1262_ENABLED || !feature_flags::SX1262_SMOKE_TEST_ENABLED || g_smoke_done) return;
  if ((millis() - g_begin_ms) < cfg::RADIO_SMOKE_TEST_DELAY_MS) return;

  g_smoke.executed = true;
  g_smoke.busy_seen = (digitalRead(PIN_SX1262_BUSY) == HIGH);

  const bool not_busy = wait_while_busy(25);
  g_smoke.status_byte = command_read1(cfg::SX1262_CMD_GET_STATUS);
  g_smoke.status_read_ok = not_busy && ((g_smoke.status_byte & cfg::SX1262_EXPECTED_STATUS_MASK) != 0x00u);

  g_smoke.error_word = command_read2(cfg::SX1262_CMD_GET_ERRORS);
  g_smoke.error_read_ok = not_busy;

  g_smoke.standby_cmd_ok = command_write1(cfg::SX1262_CMD_SET_STANDBY, cfg::SX1262_STDBY_RC);
  command_write1(cfg::SX1262_CMD_CLR_ERRORS, 0x00);

  g_smoke_done = true;
  if (g_smoke.status_read_ok && g_smoke.standby_cmd_ok) {
    g_state = "smoke-ok";
  } else if (!g_smoke.status_read_ok) {
    g_state = "status-failed";
  } else {
    g_state = "command-failed";
  }

  if (cfg::PRINT_RADIO_DEBUG) {
    Serial.printf("[sx1262] smoke status=0x%02X errors=0x%04X standby_ok=%d busy_seen=%d state=%s\n",
                  g_smoke.status_byte,
                  g_smoke.error_word,
                  g_smoke.standby_cmd_ok ? 1 : 0,
                  g_smoke.busy_seen ? 1 : 0,
                  g_state);
  }
}

uint8_t command_read1(uint8_t opcode) {
  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
  chip_select(true);
  tx_opcode(opcode);
  SPI.transfer(0x00);
  const uint8_t value = SPI.transfer(0x00);
  chip_select(false);
  SPI.endTransaction();
  return value;
}

uint16_t command_read2(uint8_t opcode) {
  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
  chip_select(true);
  tx_opcode(opcode);
  SPI.transfer(0x00);
  const uint8_t msb = SPI.transfer(0x00);
  const uint8_t lsb = SPI.transfer(0x00);
  chip_select(false);
  SPI.endTransaction();
  return static_cast<uint16_t>((static_cast<uint16_t>(msb) << 8) | lsb);
}

bool command_write1(uint8_t opcode, uint8_t value) {
  const bool not_busy = wait_while_busy(25);
  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
  chip_select(true);
  tx_opcode(opcode);
  SPI.transfer(value);
  chip_select(false);
  SPI.endTransaction();
  return not_busy;
}

int busy_level() { return digitalRead(PIN_SX1262_BUSY); }
int dio1_level() { return digitalRead(PIN_SX1262_DIO1); }
const char* state_string() { return g_state; }
bool looks_alive() {
  return g_smoke_done ? (g_smoke.status_read_ok || g_smoke.standby_cmd_ok) : (digitalRead(PIN_SX1262_BUSY) == LOW);
}
SmokeTestResult smoke_test_result() { return g_smoke; }
}
