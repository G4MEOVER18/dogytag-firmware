#include "gps.h"
#include <Arduino.h>
#include <TinyGPSPlus.h>
#include "config_defaults.h"
#include "feature_flags.h"
#include "pins.h"

namespace {
HardwareSerial GPSSerial(1);
TinyGPSPlus g_gps;
uint32_t g_bytes_seen = 0;
uint32_t g_last_byte_ms = 0;
}

namespace gps {
void begin() {
  if (!feature_flags::GPS_ENABLED) {
    Serial.println(F("[gps] disabled by feature flag"));
    return;
  }
  GPSSerial.begin(cfg::GPS_BAUD, SERIAL_8N1, static_cast<int>(PIN_GPS_RX), static_cast<int>(PIN_GPS_TX));
  Serial.printf("[gps] uart initialized at %lu baud\n", static_cast<unsigned long>(cfg::GPS_BAUD));
}

void update() {
  if (!feature_flags::GPS_ENABLED) return;
  const uint32_t slice_start = millis();
  while (GPSSerial.available() > 0) {
    const int ch = GPSSerial.read();
    g_gps.encode(static_cast<char>(ch));
    g_bytes_seen++;
    g_last_byte_ms = millis();
    if (cfg::PRINT_GPS_RAW_NMEA) Serial.write(static_cast<uint8_t>(ch));
    if ((millis() - slice_start) >= cfg::GPS_DRAIN_SLICE_MS) break;
  }
}

uint32_t bytes_seen() { return g_bytes_seen; }
uint32_t last_byte_ms() { return g_last_byte_ms; }
bool has_fix() { return g_gps.location.isValid(); }
bool utc_valid() { return g_gps.date.isValid() && g_gps.time.isValid(); }

bool utc_parts(UtcParts& out) {
  if (!utc_valid()) return false;
  out.year = g_gps.date.year();
  out.month = g_gps.date.month();
  out.day = g_gps.date.day();
  out.hour = g_gps.time.hour();
  out.minute = g_gps.time.minute();
  out.second = g_gps.time.second();
  return true;
}

double latitude_deg() { return g_gps.location.isValid() ? g_gps.location.lat() : 0.0; }
double longitude_deg() { return g_gps.location.isValid() ? g_gps.location.lng() : 0.0; }
uint8_t satellites() { return g_gps.satellites.isValid() ? static_cast<uint8_t>(g_gps.satellites.value()) : 0; }
uint32_t last_fix_age_ms() { return g_gps.location.age(); }
void format_utc(char* buffer, size_t buffer_len) {
  UtcParts utc{};
  if (!utc_parts(utc)) {
    snprintf(buffer, buffer_len, "invalid");
    return;
  }
  snprintf(buffer, buffer_len, "%04d-%02d-%02dT%02d:%02d:%02dZ", utc.year, utc.month, utc.day, utc.hour, utc.minute, utc.second);
}
}
