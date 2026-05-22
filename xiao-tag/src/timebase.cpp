#include "timebase.h"
#include <Arduino.h>
#include "config_defaults.h"
#include "feature_flags.h"
#include "gps.h"
#include "pins.h"

namespace {
volatile uint32_t g_pps_count = 0;
volatile uint32_t g_last_pps_micros = 0;
volatile bool g_pps_edge_seen = false;
timebase::LockedUtc g_locked{};
void IRAM_ATTR on_gps_pps_interrupt() {
  g_pps_count++;
  g_last_pps_micros = micros();
  g_pps_edge_seen = true;
}
}

namespace timebase {
void begin() {
  pinMode(PIN_GPS_PPS, INPUT);
  if (feature_flags::PPS_ENABLED) {
    attachInterrupt(digitalPinToInterrupt(static_cast<int>(PIN_GPS_PPS)), on_gps_pps_interrupt, cfg::PPS_INTERRUPT_MODE);
    Serial.println(F("[timebase] GPS PPS interrupt attached"));
  } else {
    Serial.println(F("[timebase] GPS PPS disabled"));
  }
}

void handle_pps_event() {
  if (!g_pps_edge_seen) return;
  noInterrupts();
  const uint32_t count = g_pps_count;
  const uint32_t last = g_last_pps_micros;
  g_pps_edge_seen = false;
  interrupts();

  gps::UtcParts utc{};
  const bool utc_ok = gps::utc_parts(utc);
  g_locked.valid = utc_ok;
  g_locked.pps_count = count;
  g_locked.pps_micros = last;
  if (utc_ok) {
    g_locked.year = utc.year;
    g_locked.month = utc.month;
    g_locked.day = utc.day;
    g_locked.hour = utc.hour;
    g_locked.minute = utc.minute;
    g_locked.second = utc.second;
  }

  if (cfg::PRINT_PPS_EVENTS) {
    if (utc_ok) {
      Serial.printf("[pps] edge=%lu micros=%lu lock=%04d-%02d-%02d %02d:%02d:%02d UTC\n",
                    static_cast<unsigned long>(count),
                    static_cast<unsigned long>(last),
                    g_locked.year, g_locked.month, g_locked.day,
                    g_locked.hour, g_locked.minute, g_locked.second);
    } else {
      Serial.printf("[pps] edge=%lu micros=%lu lock=invalid\n",
                    static_cast<unsigned long>(count),
                    static_cast<unsigned long>(last));
    }
  }
}

uint32_t pps_count() { return g_pps_count; }
uint32_t last_pps_micros() { return g_last_pps_micros; }
bool utc_synced() { return g_locked.valid; }
void utc_string(char* buffer, size_t buffer_len) {
  if (!g_locked.valid) {
    snprintf(buffer, buffer_len, "invalid");
    return;
  }
  snprintf(buffer, buffer_len, "%04d-%02d-%02dT%02d:%02d:%02dZ", g_locked.year, g_locked.month, g_locked.day, g_locked.hour, g_locked.minute, g_locked.second);
}
LockedUtc snapshot() { return g_locked; }
}
