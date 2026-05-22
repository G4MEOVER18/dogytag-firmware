#include <Arduino.h>
#include <SPI.h>

#include "app.h"
#include "build_info.h"
#include "command_console.h"
#include "config_defaults.h"
#include "gps.h"
#include "lorawan_backend.h"
#include "lorawan_config.h"
#include "lorawan_manager.h"
#include "lorawan_keys.h"
#include "mqtt_client.h"
#include "pins.h"
#include "radio_sx1262.h"
#include "radio_sx1278.h"
#include "secrets.h"
#include "telemetry.h"
#include "timebase.h"
#include "wifi_manager.h"
#include "runtime_config.h"

namespace {
uint32_t g_last_status_print_ms = 0;

void print_divider() { Serial.println(F("--------------------------------------------------")); }

void print_boot_banner() {
  print_divider();
  Serial.println(F("smarttag-firmware: boot"));
  Serial.printf("version: %s\n", APP_VERSION);
  Serial.printf("build: %s %s\n", BUILD_DATE, BUILD_TIME);
  Serial.println(F("board: Seeed XIAO ESP32S3"));
  Serial.println(F("config: XIAO + Wio-SX1262 + RA-01 + GPS(PPS)"));
  Serial.printf("lorawan: %s / %s / %s / backend=%s\n", lorawan_cfg::REGION, lorawan_cfg::VERSION, lorawan_cfg::RP_VERSION, lorawan_cfg::SELECTED_BACKEND);
#if SMARTTAG_HAS_LOCAL_SECRETS
  Serial.println(F("secrets: local"));
#else
  Serial.println(F("secrets: example placeholders"));
#endif
  print_divider();
}

void print_pin_summary() {
  if (!cfg::PRINT_PIN_SUMMARY) return;
  Serial.println(F("[pins] Shared SPI"));
  Serial.printf("  SCK  = GPIO %d\n", static_cast<int>(PIN_SPI_SCK));
  Serial.printf("  MISO = GPIO %d\n", static_cast<int>(PIN_SPI_MISO));
  Serial.printf("  MOSI = GPIO %d\n", static_cast<int>(PIN_SPI_MOSI));
  Serial.println(F("[pins] SX1262 NSS/DIO1/RST/BUSY/RFSW = 41/39/42/40/38"));
  Serial.println(F("[pins] SX1278 NSS/RST/DIO0 = 6/1/2"));
  Serial.println(F("[pins] GPS RX/TX/PPS = 44/43/3"));
  print_divider();
}

void init_shared_spi() {
  SPI.begin(static_cast<int>(PIN_SPI_SCK), static_cast<int>(PIN_SPI_MISO), static_cast<int>(PIN_SPI_MOSI), -1);
  Serial.println(F("[init] Shared SPI initialized"));
}

void print_status() {
  const uint32_t now = millis();
  if ((now - g_last_status_print_ms) < cfg::STATUS_PRINT_MS) return;
  g_last_status_print_ms = now;

  char utc_buffer[64];
  timebase::utc_string(utc_buffer, sizeof(utc_buffer));
  const auto sx1262 = radio_sx1262::smoke_test_result();
  const auto sx1278 = radio_sx1278::smoke_test_result();
  const auto lwb = lorawan_backend::runtime_info();
  const auto key_status = lorawan_keys::validate_from_secrets();

  Serial.printf("[status] uptime_ms=%lu gps_bytes=%lu pps_count=%lu utc_synced=%d fix=%d sats=%u sx1262=%s sx1262_status=0x%02X sx1262_err=0x%04X sx1278=%s sx1278_ver=0x%02X tx_ok=%d tx_done=%d rx_ok=%d rx_done=%d lorawan=%s backend=%s backend_state=%s joins=%lu wifi=%s mqtt=%s utc=%s\n",
                static_cast<unsigned long>(now),
                static_cast<unsigned long>(gps::bytes_seen()),
                static_cast<unsigned long>(timebase::pps_count()),
                timebase::utc_synced() ? 1 : 0,
                gps::has_fix() ? 1 : 0,
                static_cast<unsigned>(gps::satellites()),
                radio_sx1262::state_string(),
                sx1262.status_byte,
                sx1262.error_word,
                radio_sx1278::state_string(),
                sx1278.version_reg,
                sx1278.tx_mode_verified ? 1 : 0,
                sx1278.tx_done_seen ? 1 : 0,
                sx1278.rx_mode_verified ? 1 : 0,
                sx1278.rx_done_seen ? 1 : 0,
                lorawan_manager::state_string(),
                lwb.backend_name,
                lwb.state,
                static_cast<unsigned long>(lorawan_manager::join_attempts()),
                wifi_manager::state_string(),
                mqtt_client::state_string(),
                utc_buffer);

  if (cfg::PRINT_LORAWAN_DEBUG) {
    Serial.printf("[lorawan-status] backend=%s state=%s code=%s err=%s lib=%d real=%d slot=%s slot_header=%d slot_object=%d join_attempts=%lu keys=%d/%d/%d\n",
                  lwb.backend_name,
                  lwb.state,
                  lwb.readiness_code,
                  lwb.error_code,
                  lwb.library_available ? 1 : 0,
                  lwb.real_backend_allowed ? 1 : 0,
                  lwb.slot_state,
                  lwb.slot_header_available ? 1 : 0,
                  lwb.slot_object_scaffolded ? 1 : 0,
                  static_cast<unsigned long>(lwb.join_attempts),
                  key_status.dev_eui_ok ? 1 : 0,
                  key_status.join_eui_ok ? 1 : 0,
                  key_status.app_key_ok ? 1 : 0);
  }

  if (cfg::PRINT_JSON_TELEMETRY) {
    char payload[1300];
    telemetry::build_status_json(payload, sizeof(payload));
    Serial.printf("[json] %s\n", payload);
  }

  const uint32_t last_gps_ms = gps::last_byte_ms();
  if (last_gps_ms > 0 && (now - last_gps_ms) > cfg::GPS_SILENCE_WARNING_MS) {
    Serial.printf("[warn] no GPS UART bytes for %lu ms\n", static_cast<unsigned long>(now - last_gps_ms));
  }
}
}  // namespace

void setup() {
  Serial.begin(cfg::SERIAL_BAUD);
  delay(cfg::BOOT_BANNER_DELAY_MS);
  print_boot_banner();
  print_pin_summary();
  init_shared_spi();
  runtime_config::begin();
  gps::begin();
  timebase::begin();
  radio_sx1262::begin();
  radio_sx1278::begin();
  lorawan_manager::begin();
  wifi_manager::begin();
  mqtt_client::begin();
  app::begin();
  command_console::begin();
  Serial.println(F("[setup] completed"));
  print_divider();
}

void loop() {
  gps::update();
  timebase::handle_pps_event();
  radio_sx1262::update();
  radio_sx1278::update();
  lorawan_manager::update();
  wifi_manager::update();
  mqtt_client::update();
  app::update();
  command_console::update();
  print_status();
}
