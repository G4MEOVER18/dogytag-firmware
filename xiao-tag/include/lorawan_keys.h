#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>

namespace lorawan_keys {
struct ParseStatus {
  bool dev_eui_ok;
  bool join_eui_ok;
  bool app_key_ok;
  bool all_ok;
};

bool parse_hex(const char* input, uint8_t* out, size_t out_len);
ParseStatus validate_from_secrets();
void dev_eui_bytes(uint8_t* out, size_t out_len);
void join_eui_bytes(uint8_t* out, size_t out_len);
void app_key_bytes(uint8_t* out, size_t out_len);
}
