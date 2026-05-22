#include "lorawan_backend_slot.h"

#include <Arduino.h>
#include <Preferences.h>

#include "pins.h"

#if defined(SMARTTAG_ENABLE_RADIOLIB) && __has_include(<RadioLib.h>)
#define SMARTTAG_RADIOLIB_HEADER_AVAILABLE 1
#include <RadioLib.h>
#else
#define SMARTTAG_RADIOLIB_HEADER_AVAILABLE 0
#endif

namespace {
const char* g_state = "slot-uninitialized";
bool g_object_scaffolded = false;
bool g_node_joined = false;
float g_last_rssi = 0.0f;
float g_last_snr  = 0.0f;

#if SMARTTAG_RADIOLIB_HEADER_AVAILABLE
// g_eu868: mutable RAM copy of EU868 band.
// rx2.dr is set to 3 (DR3=SF9) in begin() to match TTN "SF9 for RX2" plan.
// Must be mutable because LoRaWAN::createSession() copies band->rx2 into
// channels[RX2] on every join attempt — a const EU868 can't be patched.
static LoRaWANBand_t g_eu868;

// Static RadioLib objects — constructed at startup, hardware init deferred to begin().
static SX1262 g_radio = new Module(
    static_cast<int>(PIN_SX1262_NSS),
    static_cast<int>(PIN_SX1262_DIO1),
    static_cast<int>(PIN_SX1262_RST),
    static_cast<int>(PIN_SX1262_BUSY),
    SPI
);
// Use &g_eu868 (not &EU868) so we can control the default RX2 data rate.
// The constructor only stores the pointer; g_eu868 is populated in begin().
static LoRaWANNode g_node(&g_radio, &g_eu868);
static bool g_radio_initialized = false;
static bool g_node_begin_done   = false;
#endif
}

namespace lorawan_backend_slot {
HardwareConfig config() {
  HardwareConfig cfg{};
  cfg.spi_sck = static_cast<int>(PIN_SPI_SCK);
  cfg.spi_miso = static_cast<int>(PIN_SPI_MISO);
  cfg.spi_mosi = static_cast<int>(PIN_SPI_MOSI);
  cfg.nss = static_cast<int>(PIN_SX1262_NSS);
  cfg.dio1 = static_cast<int>(PIN_SX1262_DIO1);
  cfg.rst = static_cast<int>(PIN_SX1262_RST);
  cfg.busy = static_cast<int>(PIN_SX1262_BUSY);
  cfg.rf_sw = static_cast<int>(PIN_SX1262_RF_SW);
  return cfg;
}

const char* slot_state_string() { return g_state; }
bool header_available() { return SMARTTAG_RADIOLIB_HEADER_AVAILABLE != 0; }
bool object_scaffolded() { return g_object_scaffolded; }

bool node_joined() { return g_node_joined; }

void begin() {
#if SMARTTAG_RADIOLIB_HEADER_AVAILABLE
  // Copy EU868 (ROM) → g_eu868 (RAM) and override RX2 DR.
  // TTN "Europe 863-870 MHz (SF9 for RX2)" sends JoinAccept on RX2 at DR3
  // (SF9 BW125 @ 869.525 MHz). RadioLib default is DR0 (SF12) — mismatch.
  // Setting rx2.dr=3 here ensures createSession() propagates it into
  // channels[RX2] on every join attempt.
  g_eu868 = EU868;
  g_eu868.rx2.dr = 3;  // DR3 = SF9 BW125 @ 869.525 MHz
  g_object_scaffolded = true;
  g_state = "header-detected-slot-ready";
  Serial.printf("[slot] band copy: EU868 rx2.dr -> 3 (SF9 for RX2)\n");
#else
  g_object_scaffolded = false;
  g_state = "no-radiolib-header";
#endif
}

int16_t perform_otaa_join(uint64_t joinEUI, uint64_t devEUI,
                           uint8_t* nwkKey, uint8_t* appKey) {
#if SMARTTAG_RADIOLIB_HEADER_AVAILABLE
  if (!g_radio_initialized) {
    // Hard-reset SX1262 so RadioLib starts with a clean chip state.
    // The smoke test (radio_sx1262.cpp) leaves the chip in a manual-SPI state
    // that RADIOLIB_ERR_CHIP_NOT_FOUND (-2) would detect without this reset.
    pinMode(static_cast<int>(PIN_SX1262_RST),  OUTPUT);
    pinMode(static_cast<int>(PIN_SX1262_BUSY), INPUT);
    digitalWrite(static_cast<int>(PIN_SX1262_RST), LOW);
    delay(5);
    digitalWrite(static_cast<int>(PIN_SX1262_RST), HIGH);
    // wait for BUSY low (chip ready), max 500 ms
    {
      const unsigned long t0 = millis();
      while (digitalRead(static_cast<int>(PIN_SX1262_BUSY)) &&
             (millis() - t0 < 500)) { yield(); }
    }
    delay(5);

    // tcxoVoltage=1.8: Wio-SX1262 uses 1.8V TCXO on DIO3 (NOT a crystal)
    // Error 0x20 = XOSC start failed if tcxoVoltage=0 is passed
    int16_t state = g_radio.begin(868.0, 125.0, 9, 7,
                                  RADIOLIB_SX126X_SYNC_WORD_PRIVATE,
                                  10, 8, 1.8f, false);
    static char g_begin_state_buf[40];
    if (state != RADIOLIB_ERR_NONE) {
      snprintf(g_begin_state_buf, sizeof(g_begin_state_buf),
               "radio-begin-failed:err%d", (int)state);
      g_state = g_begin_state_buf;
      Serial.printf("[slot] g_radio.begin() FAILED state=%d\n", (int)state);
      return state;
    }
    // Wio-SX1262 RF switch:
    //   DIO2  — SX1262 internal output → TXEN on RF switch (HIGH during TX)
    //   GPIO38 — MCU-driven RXEN → must be HIGH to enable the RX LNA path
    //
    // Without setDio2AsRfSwitch(true) the SX1262 does NOT drive DIO2 high
    // during TX → RF switch stays in RX path during TX bursts.  Even though
    // TTN still receives JoinRequests (the chip TX pin is always active), the
    // switch mismatch causes reflections back into the LNA during TX that can
    // desensitise the front-end and reduce RX sensitivity afterwards.
    //
    // Without GPIO38 = HIGH the LNA path is disabled for every RX window →
    // JoinAccept never received.
    //
    // Fix sequence:
    //   1. setDio2AsRfSwitch(true) → SX1262 drives DIO2 high during TX
    //   2. Drive GPIO38 HIGH now (LNA on)
    //   3. setRfSwitchPins(GPIO38, RADIOLIB_NC) → RadioLib keeps GPIO38 HIGH
    //      during RX and LOW during TX (RXEN logic)
    g_radio.setDio2AsRfSwitch(true);
    pinMode(static_cast<int>(PIN_SX1262_RF_SW), OUTPUT);
    digitalWrite(static_cast<int>(PIN_SX1262_RF_SW), HIGH);
    g_radio.setRfSwitchPins(static_cast<int>(PIN_SX1262_RF_SW), RADIOLIB_NC);

    g_radio_initialized = true;
    g_state = "radio-initialized";
    Serial.printf("[slot] g_radio.begin() OK, DIO2=TXEN, GPIO38=RXEN HIGH\n");
  }

  // beginOTAA() configures OTAA parameters.
  // Call only once — calling it on every retry resets RadioLib's internal
  // duty-cycle tracking, causing too-frequent JoinRequest transmissions.
  if (!g_node_begin_done) {
    int16_t bs = g_node.beginOTAA(joinEUI, devEUI, nwkKey, appKey);
    if (bs != RADIOLIB_ERR_NONE) {
      g_state = "node-begin-failed";
      return bs;
    }

    // Restore DevNonce from NVM so TTN's replay protection doesn't reject us
    // after a reboot.  beginOTAA() always resets devNonce to 0 (via clearNonces),
    // but TTN stores the last seen devNonce and rejects any JoinRequest with
    // devNonce <= stored_value.  After a reboot without persistence the device
    // would start at 0 while TTN may have stored e.g. 10, causing all joins to
    // be silently rejected.
    {
      uint8_t savedNonces[RADIOLIB_LORAWAN_NONCES_BUF_SIZE];
      Preferences prefs;
      if (prefs.begin("lorawan", true)) {  // read-only
        const size_t len = prefs.getBytes("nonces", savedNonces, sizeof(savedNonces));
        prefs.end();
        if (len == sizeof(savedNonces)) {
          const int16_t rs = g_node.setBufferNonces(savedNonces);
          // rs=0: restored; rs=-1112 (NONCES_DISCARDED): keys changed
          Serial.printf("[slot] NVM nonces restore: state=%d\n", (int)rs);
        } else {
          Serial.printf("[slot] NVM nonces: no prior save (fresh start)\n");
        }
      }
    }

    g_node.scanGuard = 200;  // 200ms guard on RX windows (EU868 timing tolerance)
    g_node_begin_done = true;
    // Log expected RX2 parameters so they can be verified against TTN config.
    // TTN "Europe 863-870 MHz (SF9 for RX2)": rx2.dr=3 (SF9 BW125 @869.525 MHz)
    // TTN "Europe 863-870 MHz" (standard):    rx2.dr=0 (SF12 BW125 @869.525 MHz)
    // If join keeps failing, check TTN Console → App → Frequency Plan and
    // set g_eu868.rx2.dr accordingly in begin() above.
    Serial.printf("[slot] node configured: scanGuard=200ms, rx2.dr=%u (DR%u=%s), rx2.freq=%.3fMHz\n",
                  (unsigned)g_eu868.rx2.dr,
                  (unsigned)g_eu868.rx2.dr,
                  g_eu868.rx2.dr == 0 ? "SF12" : g_eu868.rx2.dr == 3 ? "SF9" : "?",
                  g_eu868.rx2.freq / 10000.0);
  }

  g_state = "otaa-joining";
  // activateOTAA() is blocking — waits for JoinAccept (up to ~7s per attempt).
  // Returns RADIOLIB_LORAWAN_NEW_SESSION (-1118) on fresh join,
  // RADIOLIB_LORAWAN_SESSION_RESTORED (-1117) if NVS session restored,
  // RADIOLIB_ERR_NO_JOIN_ACCEPT (-1116) if no response received.
  int16_t state = g_node.activateOTAA();

  // Save DevNonce to NVM after every attempt (success or failure).
  // This ensures the next boot starts with the correct devNonce and TTN
  // never sees a replay (devNonce must be strictly increasing).
  {
    const uint8_t* buf = g_node.getBufferNonces();
    Preferences prefs;
    if (prefs.begin("lorawan", false)) {
      prefs.putBytes("nonces", buf, RADIOLIB_LORAWAN_NONCES_BUF_SIZE);
      prefs.end();
    }
    // DevNonce is at RADIOLIB_LORAWAN_NONCES_DEV_NONCE offset, big-endian
    const uint16_t dn = static_cast<uint16_t>(
        (buf[RADIOLIB_LORAWAN_NONCES_DEV_NONCE] << 8) |
         buf[RADIOLIB_LORAWAN_NONCES_DEV_NONCE + 1]);
    Serial.printf("[slot] NVM nonces saved devNonce=%u activateOTAA=%d\n",
                  (unsigned)dn, (int)state);
  }

  // Post-join diagnostics: GPIO38 state + CAD to verify RX path is alive.
  if (state != RADIOLIB_ERR_NONE && state != -1117 && state != -1118) {
    int gpio38 = digitalRead(static_cast<int>(PIN_SX1262_RF_SW));
    Serial.printf("[slot-diag] activateOTAA ret=%d gpio38=%d\n", (int)state, gpio38);
    Serial.printf("[slot-diag] rx2.freq=%.3fMHz rx2.dr=%u scanGuard=%dms\n",
                  g_eu868.rx2.freq / 10000.0,
                  (unsigned)g_eu868.rx2.dr,
                  (int)g_node.scanGuard);
    Serial.printf("[slot-diag] If -1116: RX windows opened but empty.\n"
                  "[slot-diag]   => Check TTN Console: App > Frequency Plan\n"
                  "[slot-diag]   => 'SF9 for RX2' needs rx2.dr=3 (current=%u)\n"
                  "[slot-diag]   => Standard EU868 needs rx2.dr=0\n"
                  "[slot-diag]   => Also verify a TTN gateway with TX capability is nearby.\n",
                  (unsigned)g_eu868.rx2.dr);

    // Quick CAD scan on RX2 channel to verify SX1262 RX path hardware.
    g_radio.setFrequency(g_eu868.rx2.freq / 10000.0);
    g_radio.setSpreadingFactor(9);
    g_radio.setBandwidth(125.0);
    int16_t cad = g_radio.scanChannel();
    Serial.printf("[slot-diag] CAD %.3fMHz SF9: %s (%d)\n",
                  g_eu868.rx2.freq / 10000.0,
                  (cad == RADIOLIB_LORA_DETECTED) ? "LORA_DETECTED" :
                  (cad == RADIOLIB_CHANNEL_FREE)  ? "CHANNEL_FREE"  : "CAD_ERROR",
                  (int)cad);
  }

  const bool joined = (state == RADIOLIB_ERR_NONE ||
                       state == -1117 /* SESSION_RESTORED */ ||
                       state == -1118 /* NEW_SESSION */);
  if (joined) {
    g_node_joined = true;
    g_state = "otaa-joined";
    g_last_rssi = g_radio.getRSSI();
    g_last_snr  = g_radio.getSNR();
    Serial.printf("[slot] join OK rssi=%.1f snr=%.1f\n", g_last_rssi, g_last_snr);
  } else {
    g_node_joined = false;
    g_state = "otaa-join-failed";
  }
  return joined ? RADIOLIB_ERR_NONE : state;
#else
  (void)joinEUI; (void)devEUI; (void)nwkKey; (void)appKey;
  return -1;
#endif
}

int16_t send_uplink(uint8_t port, uint8_t* data, size_t len, bool confirmed) {
#if SMARTTAG_RADIOLIB_HEADER_AVAILABLE
  if (!g_node_joined) { return -1; }
  int16_t state = g_node.sendReceive(data, len, port, nullptr, 0, confirmed);
  if (state == RADIOLIB_ERR_NONE || state == RADIOLIB_LORAWAN_DOWNLINK) {
    g_last_rssi = g_radio.getRSSI();
    g_last_snr  = g_radio.getSNR();
  }
  return state;
#else
  (void)port; (void)data; (void)len; (void)confirmed;
  return -1;
#endif
}

float last_rssi() { return g_last_rssi; }
float last_snr()  { return g_last_snr;  }
}
