#include "lorawan_keys.h"

#include <ctype.h>
#include <string.h>

#include "secrets.h"

namespace {
int hex_nibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
  if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
  return -1;
}

bool parse_fixed(const char* input, uint8_t* out, size_t out_len) {
  if (input == nullptr || out == nullptr) return false;
  const size_t needed = out_len * 2;
  if (strlen(input) != needed) return false;
  for (size_t i = 0; i < out_len; ++i) {
    const int hi = hex_nibble(input[i * 2]);
    const int lo = hex_nibble(input[i * 2 + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = static_cast<uint8_t>((hi << 4) | lo);
  }
  return true;
}
}

namespace lorawan_keys {
bool parse_hex(const char* input, uint8_t* out, size_t out_len) { return parse_fixed(input, out, out_len); }

ParseStatus validate_from_secrets() {
  ParseStatus s{};
  uint8_t dev[8]{};
  uint8_t join[8]{};
  uint8_t key[16]{};
  s.dev_eui_ok = parse_fixed(secrets::lorawan_dev_eui(), dev, sizeof(dev));
  s.join_eui_ok = parse_fixed(secrets::lorawan_join_eui(), join, sizeof(join));
  s.app_key_ok = parse_fixed(secrets::lorawan_app_key(), key, sizeof(key));
  s.all_ok = s.dev_eui_ok && s.join_eui_ok && s.app_key_ok;
  return s;
}

void dev_eui_bytes(uint8_t* out, size_t out_len) { if(out_len>=8) parse_fixed(secrets::lorawan_dev_eui(), out, 8); }
void join_eui_bytes(uint8_t* out, size_t out_len) { if(out_len>=8) parse_fixed(secrets::lorawan_join_eui(), out, 8); }
void app_key_bytes(uint8_t* out, size_t out_len) { if(out_len>=16) parse_fixed(secrets::lorawan_app_key(), out, 16); }
}
