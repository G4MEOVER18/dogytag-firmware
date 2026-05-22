#include "radio_sx1278.h"

#include <Arduino.h>
#include <SPI.h>
#include <cstring>

#include "config_defaults.h"
#include "feature_flags.h"
#include "pins.h"
#include "runtime_config.h"

namespace {
const char* g_state = "disabled";
radio_sx1278::SmokeTestResult g_smoke{};
bool g_smoke_done = false;
bool g_test_beacon_requested = false;
bool g_tx_test_started = false;
bool g_rx_test_started = false;
uint32_t g_begin_ms = 0;
uint32_t g_tx_start_ms = 0;
uint32_t g_rx_start_ms = 0;

inline void chip_select(bool active) { digitalWrite(PIN_SX1278_NSS, active ? LOW : HIGH); }
void pulse_reset() { digitalWrite(PIN_SX1278_RST, LOW); delay(2); digitalWrite(PIN_SX1278_RST, HIGH); delay(10); }
uint8_t spi_transfer(uint8_t value) { return SPI.transfer(value); }

void write_fifo_payload(const uint8_t* data, size_t len) {
  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
  chip_select(true);
  spi_transfer(cfg::SX1278_REG_FIFO | 0x80u);
  for (size_t i = 0; i < len; ++i) spi_transfer(data[i]);
  chip_select(false);
  SPI.endTransaction();
}

void clear_irq_flags() { radio_sx1278::write_register(cfg::SX1278_REG_IRQ_FLAGS, 0xFFu); }

void prepare_tx_scaffold() {
  const char* payload = runtime_config::beacon_text();
  const size_t payload_len = strlen(payload);
  radio_sx1278::write_register(cfg::SX1278_REG_OP_MODE, static_cast<uint8_t>(cfg::SX1278_MODE_LONG_RANGE | cfg::SX1278_MODE_STDBY));
  radio_sx1278::write_register(cfg::SX1278_REG_FRF_MSB, cfg::SX1278_TEST_FRF_MSB);
  radio_sx1278::write_register(cfg::SX1278_REG_FRF_MID, cfg::SX1278_TEST_FRF_MID);
  radio_sx1278::write_register(cfg::SX1278_REG_FRF_LSB, cfg::SX1278_TEST_FRF_LSB);
  radio_sx1278::write_register(cfg::SX1278_REG_PA_CONFIG, cfg::SX1278_TEST_PA_CONFIG);
  radio_sx1278::write_register(cfg::SX1278_REG_FIFO_TX_BASE_ADDR, 0x00u);
  radio_sx1278::write_register(cfg::SX1278_REG_FIFO_ADDR_PTR, 0x00u);
  write_fifo_payload(reinterpret_cast<const uint8_t*>(payload), payload_len);
  radio_sx1278::write_register(cfg::SX1278_REG_PAYLOAD_LENGTH, static_cast<uint8_t>(payload_len));
  g_smoke.payload_length = static_cast<uint8_t>(payload_len);
  g_smoke.tx_scaffold_prepared = true;
  g_state = "tx-scaffold-ready";
}

void begin_tx_mode_test() {
  radio_sx1278::write_register(cfg::SX1278_REG_DIO_MAPPING_1, cfg::SX1278_DIO0_TXDONE_MAPPING);
  clear_irq_flags();
  radio_sx1278::write_register(cfg::SX1278_REG_OP_MODE, static_cast<uint8_t>(cfg::SX1278_MODE_LONG_RANGE | cfg::SX1278_MODE_TX));
  delay(2);
  g_smoke.opmode_tx = radio_sx1278::read_register(cfg::SX1278_REG_OP_MODE);
  g_smoke.tx_mode_entered = true;
  g_smoke.tx_mode_verified = ((g_smoke.opmode_tx & 0x87u) == static_cast<uint8_t>(cfg::SX1278_MODE_LONG_RANGE | cfg::SX1278_MODE_TX));
  g_tx_test_started = true;
  g_tx_start_ms = millis();
  ++g_smoke.tx_attempts;
  g_state = g_smoke.tx_mode_verified ? "tx-test-running" : "tx-mode-failed";
}

void begin_rx_mode_test() {
  radio_sx1278::write_register(cfg::SX1278_REG_DIO_MAPPING_1, cfg::SX1278_DIO0_RXDONE_MAPPING);
  radio_sx1278::write_register(cfg::SX1278_REG_FIFO_RX_BASE_ADDR, 0x00u);
  radio_sx1278::write_register(cfg::SX1278_REG_FIFO_ADDR_PTR, 0x00u);
  clear_irq_flags();
  radio_sx1278::write_register(cfg::SX1278_REG_OP_MODE, static_cast<uint8_t>(cfg::SX1278_MODE_LONG_RANGE | cfg::SX1278_MODE_RXCONTINUOUS));
  delay(2);
  g_smoke.opmode_rx = radio_sx1278::read_register(cfg::SX1278_REG_OP_MODE);
  g_smoke.rx_mode_entered = true;
  g_smoke.rx_mode_verified = ((g_smoke.opmode_rx & 0x87u) == static_cast<uint8_t>(cfg::SX1278_MODE_LONG_RANGE | cfg::SX1278_MODE_RXCONTINUOUS));
  g_rx_test_started = true;
  g_rx_start_ms = millis();
  ++g_smoke.rx_attempts;
  g_state = g_smoke.rx_mode_verified ? "rx-test-running" : "rx-mode-failed";
}
}  // namespace

namespace radio_sx1278 {
void begin() {
  if (!feature_flags::SX1278_ENABLED) {
    g_state = "disabled";
    Serial.println(F("[sx1278] disabled by feature flag"));
    return;
  }
  pinMode(PIN_SX1278_NSS, OUTPUT);
  chip_select(false);
  pinMode(PIN_SX1278_RST, OUTPUT);
  digitalWrite(PIN_SX1278_RST, HIGH);
  pinMode(PIN_SX1278_DIO0, INPUT);
  pulse_reset();
  g_smoke.reset_pulsed = true;
  g_begin_ms = millis();
  g_state = feature_flags::SX1278_SMOKE_TEST_ENABLED ? "smoke-pending" : "ready";
  Serial.println(F("[sx1278] initialized"));
}

void update() {
  if (!feature_flags::SX1278_ENABLED) return;

  if (!g_smoke_done && feature_flags::SX1278_SMOKE_TEST_ENABLED) {
    if ((millis() - g_begin_ms) < cfg::RADIO_SMOKE_TEST_DELAY_MS) return;
    g_smoke.executed = true;
    g_smoke.nss_ok = true;
    g_smoke.version_reg = read_register(cfg::SX1278_REG_VERSION);
    g_smoke.version_read_ok = (g_smoke.version_reg != 0x00u && g_smoke.version_reg != 0xFFu);
    if (feature_flags::SX1278_MODE_TEST_ENABLED) {
      const uint8_t sleep_value = static_cast<uint8_t>(cfg::SX1278_MODE_LONG_RANGE | cfg::SX1278_MODE_SLEEP);
      write_register(cfg::SX1278_REG_OP_MODE, sleep_value);
      delay(2);
      g_smoke.opmode_sleep = read_register(cfg::SX1278_REG_OP_MODE);
      const uint8_t standby_value = static_cast<uint8_t>(cfg::SX1278_MODE_LONG_RANGE | cfg::SX1278_MODE_STDBY);
      write_register(cfg::SX1278_REG_OP_MODE, standby_value);
      delay(2);
      g_smoke.opmode_standby = read_register(cfg::SX1278_REG_OP_MODE);
      g_smoke.mode_write_ok = ((g_smoke.opmode_sleep & 0x87u) == sleep_value) &&
                              ((g_smoke.opmode_standby & 0x87u) == standby_value);
    }
    g_smoke_done = true;
    if (g_smoke.version_reg == cfg::SX1278_EXPECTED_VERSION && (!feature_flags::SX1278_MODE_TEST_ENABLED || g_smoke.mode_write_ok)) {
      g_state = "smoke-ok";
      if (feature_flags::SX1278_TX_SCAFFOLD_ENABLED) request_test_beacon();
    } else if (!g_smoke.version_read_ok) {
      g_state = "smoke-failed";
    } else if (feature_flags::SX1278_MODE_TEST_ENABLED && !g_smoke.mode_write_ok) {
      g_state = "mode-test-failed";
    } else {
      g_state = "smoke-unexpected-version";
    }
    if (cfg::PRINT_RADIO_DEBUG) {
      Serial.printf("[sx1278] smoke version=0x%02X expected=0x%02X op_sleep=0x%02X op_standby=0x%02X state=%s\n",
                    g_smoke.version_reg, cfg::SX1278_EXPECTED_VERSION, g_smoke.opmode_sleep, g_smoke.opmode_standby, g_state);
    }
    return;
  }

  if (g_test_beacon_requested && g_smoke_done && smoke_test_ok() && !g_smoke.tx_scaffold_prepared) {
    prepare_tx_scaffold();
    if (cfg::PRINT_RADIO_DEBUG) {
      Serial.printf("[sx1278] tx scaffold prepared payload_len=%u freq=%luHz state=%s\n",
                    static_cast<unsigned>(g_smoke.payload_length), static_cast<unsigned long>(cfg::SX1278_TEST_FREQUENCY_HZ), g_state);
    }
    return;
  }

  if (feature_flags::SX1278_TX_TEST_ENABLED && g_test_beacon_requested && g_smoke.tx_scaffold_prepared && !g_tx_test_started) {
    begin_tx_mode_test();
    if (cfg::PRINT_RADIO_DEBUG) {
      Serial.printf("[sx1278] tx mode request op_tx=0x%02X verified=%d attempts=%lu state=%s\n",
                    g_smoke.opmode_tx, g_smoke.tx_mode_verified ? 1 : 0, static_cast<unsigned long>(g_smoke.tx_attempts), g_state);
    }
    return;
  }

  if (g_tx_test_started && !g_smoke.tx_timeout_seen && !g_smoke.tx_done_seen) {
    g_smoke.tx_irq_flags = read_register(cfg::SX1278_REG_IRQ_FLAGS);
    if ((g_smoke.tx_irq_flags & cfg::SX1278_IRQ_TX_DONE_MASK) != 0u || digitalRead(PIN_SX1278_DIO0) == HIGH) {
      g_smoke.tx_done_seen = true;
      g_state = "tx-done-seen";
      clear_irq_flags();
      write_register(cfg::SX1278_REG_OP_MODE, static_cast<uint8_t>(cfg::SX1278_MODE_LONG_RANGE | cfg::SX1278_MODE_STDBY));
      if (cfg::PRINT_RADIO_DEBUG) {
        Serial.printf("[sx1278] tx done irq=0x%02X attempts=%lu state=%s\n",
                      g_smoke.tx_irq_flags, static_cast<unsigned long>(g_smoke.tx_attempts), g_state);
      }
      return;
    }
    if ((millis() - g_tx_start_ms) >= cfg::SX1278_TX_TEST_TIMEOUT_MS) {
      g_smoke.tx_timeout_seen = true;
      g_state = "tx-timeout";
      write_register(cfg::SX1278_REG_OP_MODE, static_cast<uint8_t>(cfg::SX1278_MODE_LONG_RANGE | cfg::SX1278_MODE_STDBY));
      if (cfg::PRINT_RADIO_DEBUG) {
        Serial.printf("[sx1278] tx test timeout after %lu ms irq=0x%02X state=%s\n",
                      static_cast<unsigned long>(cfg::SX1278_TX_TEST_TIMEOUT_MS), g_smoke.tx_irq_flags, g_state);
      }
      return;
    }
  }

  if (feature_flags::SX1278_RX_TEST_ENABLED && tx_test_completed() && !g_rx_test_started) {
    begin_rx_mode_test();
    if (cfg::PRINT_RADIO_DEBUG) {
      Serial.printf("[sx1278] rx mode request op_rx=0x%02X verified=%d attempts=%lu state=%s\n",
                    g_smoke.opmode_rx, g_smoke.rx_mode_verified ? 1 : 0, static_cast<unsigned long>(g_smoke.rx_attempts), g_state);
    }
    return;
  }

  if (g_rx_test_started && !g_smoke.rx_timeout_seen && !g_smoke.rx_done_seen) {
    g_smoke.rx_irq_flags = read_register(cfg::SX1278_REG_IRQ_FLAGS);
    if ((g_smoke.rx_irq_flags & cfg::SX1278_IRQ_RX_DONE_MASK) != 0u || digitalRead(PIN_SX1278_DIO0) == HIGH) {
      g_smoke.rx_done_seen = true;
      g_state = "rx-done-seen";
      clear_irq_flags();
      write_register(cfg::SX1278_REG_OP_MODE, static_cast<uint8_t>(cfg::SX1278_MODE_LONG_RANGE | cfg::SX1278_MODE_STDBY));
      if (cfg::PRINT_RADIO_DEBUG) {
        Serial.printf("[sx1278] rx done irq=0x%02X attempts=%lu state=%s\n",
                      g_smoke.rx_irq_flags, static_cast<unsigned long>(g_smoke.rx_attempts), g_state);
      }
      return;
    }
    if ((millis() - g_rx_start_ms) >= cfg::SX1278_RX_TEST_TIMEOUT_MS) {
      g_smoke.rx_timeout_seen = true;
      g_state = "rx-timeout";
      write_register(cfg::SX1278_REG_OP_MODE, static_cast<uint8_t>(cfg::SX1278_MODE_LONG_RANGE | cfg::SX1278_MODE_STDBY));
      if (cfg::PRINT_RADIO_DEBUG) {
        Serial.printf("[sx1278] rx test timeout after %lu ms irq=0x%02X state=%s\n",
                      static_cast<unsigned long>(cfg::SX1278_RX_TEST_TIMEOUT_MS), g_smoke.rx_irq_flags, g_state);
      }
    }
  }
}

uint8_t read_register(uint8_t reg) {
  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
  chip_select(true);
  spi_transfer(reg & 0x7Fu);
  const uint8_t value = spi_transfer(0x00);
  chip_select(false);
  SPI.endTransaction();
  return value;
}

void write_register(uint8_t reg, uint8_t value) {
  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
  chip_select(true);
  spi_transfer(reg | 0x80u);
  spi_transfer(value);
  chip_select(false);
  SPI.endTransaction();
}

void request_test_beacon() {
  g_test_beacon_requested = true;
  if (g_smoke_done && smoke_test_ok() && !g_smoke.tx_scaffold_prepared) g_state = "tx-scaffold-pending";
}

bool test_beacon_requested() { return g_test_beacon_requested; }
const char* current_payload_text() { return runtime_config::beacon_text(); }
int dio0_level() { return digitalRead(PIN_SX1278_DIO0); }
const char* state_string() { return g_state; }
bool smoke_test_ok() {
  const bool version_ok = g_smoke_done && (g_smoke.version_reg == cfg::SX1278_EXPECTED_VERSION);
  const bool mode_ok = !feature_flags::SX1278_MODE_TEST_ENABLED || g_smoke.mode_write_ok;
  return version_ok && mode_ok;
}
bool tx_scaffold_ready() { return g_smoke.tx_scaffold_prepared; }
bool tx_test_completed() { return g_smoke.tx_timeout_seen || g_smoke.tx_done_seen; }
bool rx_test_completed() { return g_smoke.rx_timeout_seen || g_smoke.rx_done_seen; }
SmokeTestResult smoke_test_result() { return g_smoke; }
}  // namespace radio_sx1278
