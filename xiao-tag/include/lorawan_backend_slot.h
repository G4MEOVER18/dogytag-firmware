#pragma once

#include <Arduino.h>

namespace lorawan_backend_slot {
struct HardwareConfig {
  int spi_sck;
  int spi_miso;
  int spi_mosi;
  int nss;
  int dio1;
  int rst;
  int busy;
  int rf_sw;
};

HardwareConfig config();
const char* slot_state_string();
bool header_available();
bool object_scaffolded();
bool node_joined();
void begin();

// Performs blocking OTAA join via RadioLib SX1262.
// Returns RADIOLIB_ERR_NONE (0) on success.
// nwkKey == appKey for LoRaWAN 1.0.x.
int16_t perform_otaa_join(uint64_t joinEUI, uint64_t devEUI,
                           uint8_t* nwkKey, uint8_t* appKey);

// Send a confirmed/unconfirmed uplink after join.
int16_t send_uplink(uint8_t port, uint8_t* data, size_t len, bool confirmed = false);

// RSSI/SNR of the last received LoRaWAN packet (JoinAccept or downlink).
float last_rssi();
float last_snr();
}
