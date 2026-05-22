#include "lorawan_backend_radiolib.h"

#include <Arduino.h>
#include <cstring>
#include <stdint.h>

#include "config_defaults.h"
#include "feature_flags.h"
#include "lorawan_backend_slot.h"
#include "lorawan_config.h"
#include "lorawan_keys.h"
#include "radio_sx1262.h"
#include "secrets.h"

#if defined(SMARTTAG_ENABLE_RADIOLIB) && __has_include(<RadioLib.h>)
#define SMARTTAG_RADIOLIB_HEADER_AVAILABLE 1
#include <RadioLib.h>
#else
#define SMARTTAG_RADIOLIB_HEADER_AVAILABLE 0
#endif

namespace {
const char* g_state = "disabled";
const char* g_readiness_code = "boot";
const char* g_error_code = "none";
unsigned long g_join_attempts = 0;
bool g_initialized = false;
bool g_join_supported = false;
bool g_join_started = false;
bool g_joined = false;
uint32_t g_last_action_ms = 0;

bool has_non_placeholder(const char* v) {
  return v != nullptr && v[0] != '\0' && strstr(v, "REPLACE_") == nullptr;
}

bool secrets_ok() {
  const auto parsed = lorawan_keys::validate_from_secrets();
  return has_non_placeholder(secrets::lorawan_dev_eui()) &&
         has_non_placeholder(secrets::lorawan_join_eui()) &&
         has_non_placeholder(secrets::lorawan_app_key()) &&
         parsed.all_ok;
}

void set_state(const char* state) {
  g_state = state;
  g_last_action_ms = millis();
}

void set_error(const char* code) {
  g_error_code = code;
}

void set_readiness(const char* code) {
  g_readiness_code = code;
}
}  // namespace

namespace lorawan_backend_radiolib {
void begin() {
  g_initialized = false;
  g_join_supported = false;
  g_join_started = false;
  g_joined = false;
  g_join_attempts = 0;
  set_error("none");

  if (!feature_flags::LORAWAN_FRAMEWORK_ENABLED) {
    set_readiness("framework-disabled");
    set_state("disabled");
    return;
  }
  if (!radio_sx1262::looks_alive()) {
    set_readiness("waiting-radio");
    set_error("sx1262-not-ready");
    set_state("waiting-radio");
    return;
  }
  if (!secrets_ok()) {
    set_readiness("missing-secrets");
    set_error("otaa-format-invalid");
    set_state("missing-secrets");
    return;
  }

  g_initialized = true;
  g_join_supported = true;

  if (!library_available()) {
    set_readiness("adapter-no-lib");
    set_error("radiolib-header-missing");
    set_state("adapter-ready-no-lib");
  } else if (!real_backend_allowed()) {
    set_readiness("adapter-lib-only");
    set_error("real-backend-disabled");
    set_state("adapter-ready-lib-detected");
  } else if (!lorawan_backend_slot::object_scaffolded()) {
    set_readiness("slot-not-ready");
    set_error("slot-object-missing");
    set_state("real-backend-slot-blocked");
  } else {
    set_readiness("real-slot-ready");
    set_error("none");
    set_state("real-backend-slot-ready");
  }

  if (cfg::PRINT_LORAWAN_DEBUG) {
    Serial.printf("[lorawan-backend] adapter=%s state=%s region=%s lib=%d real=%d possible=%d slot=%s code=%s err=%s\n",
                  lorawan_cfg::SELECTED_BACKEND,
                  g_state,
                  lorawan_cfg::REGION,
                  library_available() ? 1 : 0,
                  real_backend_allowed() ? 1 : 0,
                  real_backend_possible() ? 1 : 0,
                  lorawan_backend_slot::slot_state_string(),
                  g_readiness_code,
                  g_error_code);
  }
}

void update() {
  if (!g_initialized) {
    return;
  }
  if (!g_join_started || g_joined) {
    return;
  }

  const uint32_t now = millis();
  if ((now - g_last_action_ms) >= cfg::LORAWAN_BACKEND_JOIN_TICK_MS) {
    ++g_join_attempts;
    if (real_backend_possible()) {
      set_readiness("real-join-slot-pending");
      set_state("real-join-slot-pending");
    } else if (library_available() && real_backend_allowed()) {
      set_readiness("real-join-blocked");
      set_error("slot-not-fully-implemented");
      set_state("real-join-blocked-by-config");
    } else {
      set_readiness("simulated-join");
      set_state("join-simulated-pending");
    }

    if (cfg::PRINT_LORAWAN_DEBUG) {
      Serial.printf("[lorawan-backend] join tick backend=%s attempts=%lu radio=%s state=%s code=%s err=%s\n",
                    lorawan_cfg::SELECTED_BACKEND,
                    g_join_attempts,
                    radio_sx1262::state_string(),
                    g_state,
                    g_readiness_code,
                    g_error_code);
    }
  }
}

bool can_start_join() {
  return g_initialized && g_join_supported && !g_joined;
}

bool start_join() {
  if (!can_start_join()) {
    return false;
  }
  g_join_started = true;

  if (real_backend_possible() && lorawan_backend_slot::object_scaffolded()) {
    set_readiness("join-start-real-slot");
    set_state("join-started-real-slot");

    // Build key material
    uint8_t joinEUI_b[8], devEUI_b[8], appKey_b[16];
    lorawan_keys::join_eui_bytes(joinEUI_b, sizeof(joinEUI_b));
    lorawan_keys::dev_eui_bytes(devEUI_b,  sizeof(devEUI_b));
    lorawan_keys::app_key_bytes(appKey_b,  sizeof(appKey_b));

    // Convert byte arrays to uint64_t (MSB-first)
    uint64_t joinEUI64 = 0, devEUI64 = 0;
    for (int i = 0; i < 8; ++i) {
      joinEUI64 = (joinEUI64 << 8) | joinEUI_b[i];
      devEUI64  = (devEUI64  << 8) | devEUI_b[i];
    }

    if (cfg::PRINT_LORAWAN_DEBUG) {
      Serial.printf("[lorawan-backend] OTAA start devEUI=%016llX joinEUI=%016llX\n",
                    devEUI64, joinEUI64);
    }

    // LoRaWAN 1.0.x: nwkKey == appKey
    int16_t result = lorawan_backend_slot::perform_otaa_join(
        joinEUI64, devEUI64, appKey_b, appKey_b);

    if (result == 0 /* RADIOLIB_ERR_NONE */) {
      g_joined = true;
      set_readiness("otaa-success");
      set_error("none");
      set_state("joined");
      Serial.printf("[lorawan-backend] OTAA join SUCCESS\n");
    } else {
      g_join_started = false;  // allow retry
      set_error("otaa-failed");
      set_readiness("otaa-failed");
      set_state("join-failed");
      Serial.printf("[lorawan-backend] OTAA join FAILED code=%d\n", result);
    }
  } else {
    set_readiness("join-start-scaffold");
    set_state("join-started-scaffold");
  }

  if (cfg::PRINT_LORAWAN_DEBUG) {
    Serial.printf("[lorawan-backend] join start backend=%s lib=%d real=%d possible=%d code=%s err=%s\n",
                  lorawan_cfg::SELECTED_BACKEND,
                  library_available() ? 1 : 0,
                  real_backend_allowed() ? 1 : 0,
                  real_backend_possible() ? 1 : 0,
                  g_readiness_code,
                  g_error_code);
  }
  return true;
}

int16_t send_uplink(uint8_t port, uint8_t* data, size_t len, bool confirmed) {
  if (!g_joined) return -1;
  return lorawan_backend_slot::send_uplink(port, data, len, confirmed);
}

const char* state_string() { return g_state; }
unsigned long join_attempts() { return g_join_attempts; }
bool initialized() { return g_initialized; }
bool join_supported() { return g_join_supported; }
bool joined() { return g_joined; }
bool library_available() { return SMARTTAG_RADIOLIB_HEADER_AVAILABLE == 1; }
bool real_backend_allowed() { return feature_flags::LORAWAN_BACKEND_REAL_RADIOLIB_ALLOWED; }
bool real_backend_possible() { return library_available() && real_backend_allowed(); }
const char* readiness_code() { return g_readiness_code; }
const char* error_code() { return g_error_code; }
}  // namespace lorawan_backend_radiolib
