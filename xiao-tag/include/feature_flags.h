#pragma once
namespace feature_flags {
// Core radios & GPS
static constexpr bool WIFI_ENABLED = true;
static constexpr bool MQTT_ENABLED = true;
static constexpr bool SX1262_ENABLED = true;
static constexpr bool SX1278_ENABLED = true;
static constexpr bool GPS_ENABLED = true;
static constexpr bool PPS_ENABLED = true;

// SX1278 tests (433 MHz smoke/functional tests on boot)
static constexpr bool SX1278_SMOKE_TEST_ENABLED = true;
static constexpr bool SX1278_MODE_TEST_ENABLED = true;
static constexpr bool SX1278_TX_SCAFFOLD_ENABLED = true;
static constexpr bool SX1278_TX_TEST_ENABLED = true;
static constexpr bool SX1278_RX_TEST_ENABLED = true;

// SX1262 smoke test
static constexpr bool SX1262_SMOKE_TEST_ENABLED = true;

// LoRaWAN — real RadioLib backend aktiviert für v3.0.0
static constexpr bool LORAWAN_FRAMEWORK_ENABLED = true;
static constexpr bool LORAWAN_AUTOSTART_JOIN_SCAFFOLD = true;
static constexpr bool LORAWAN_BACKEND_RADIOLIB_SELECTED = true;
static constexpr bool LORAWAN_BACKEND_REAL_RADIOLIB_ALLOWED = true;  // v3.0.0: aktiviert
}
