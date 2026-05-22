#pragma once
#include <Arduino.h>
namespace timebase {
struct LockedUtc {
  bool valid;
  uint32_t pps_count;
  uint32_t pps_micros;
  int year;
  int month;
  int day;
  int hour;
  int minute;
  int second;
};
void begin();
void handle_pps_event();
uint32_t pps_count();
uint32_t last_pps_micros();
bool utc_synced();
void utc_string(char* buffer, size_t buffer_len);
LockedUtc snapshot();
}
