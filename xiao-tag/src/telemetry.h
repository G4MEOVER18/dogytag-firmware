#pragma once
#include <Arduino.h>
namespace telemetry {
void build_status_json(char* buffer, size_t buffer_len);
void build_gps_json(char* buffer, size_t buffer_len);
void build_lora_json(char* buffer, size_t buffer_len);
void build_telemetry_json(char* buffer, size_t buffer_len);
}
