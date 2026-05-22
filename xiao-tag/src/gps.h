#pragma once
#include <Arduino.h>
namespace gps {
struct UtcParts {
  int year;
  int month;
  int day;
  int hour;
  int minute;
  int second;
};
void begin();
void update();
uint32_t bytes_seen();
uint32_t last_byte_ms();
bool has_fix();
bool utc_valid();
bool utc_parts(UtcParts& out);
double latitude_deg();
double longitude_deg();
uint8_t satellites();
uint32_t last_fix_age_ms();
void format_utc(char* buffer, size_t buffer_len);
}
