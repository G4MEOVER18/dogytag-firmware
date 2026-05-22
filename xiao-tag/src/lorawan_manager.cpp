#include "lorawan_manager.h"

#include <Arduino.h>
#include <cstring>

#include "config_defaults.h"
#include "feature_flags.h"
#include "lorawan_backend.h"
#include "lorawan_config.h"
#include "lorawan_keys.h"
#include "radio_sx1262.h"
#include "runtime_config.h"
#include "secrets.h"

namespace {
const char* g_state = "disabled";
uint32_t g_last_retry_ms = 0;
uint32_t g_last_action_ms = 0;
bool g_join_requested = false;
bool g_radio_ready = false;
bool g_secrets_ok = false;

bool has_non_placeholder(const char* v) {
  return v != nullptr && v[0] != '\0' && strstr(v, "REPLACE_") == nullptr;
}

void set_state(const char* next) {
  g_state = next;
  g_last_action_ms = millis();
}
}  // namespace

namespace lorawan_manager {
void begin() {
  if (!feature_flags::LORAWAN_FRAMEWORK_ENABLED) {
    set_state("disabled");
    Serial.println(F("[lorawan] framework disabled by feature flag"));
    return;
  }

  g_radio_ready = radio_sx1262::looks_alive();
  if (!g_radio_ready) {
    set_state("waiting-radio");
    Serial.println(F("[lorawan] waiting for SX1262 bring-up"));
    return;
  }

  const auto key_status = lorawan_keys::validate_from_secrets();
  g_secrets_ok = secrets_present() && key_status.all_ok;
  if (!g_secrets_ok) {
    set_state("missing-secrets");
    Serial.printf("[lorawan] OTAA keys invalid or placeholders dev=%d join=%d app=%d\n",
                  key_status.dev_eui_ok ? 1 : 0,
                  key_status.join_eui_ok ? 1 : 0,
                  key_status.app_key_ok ? 1 : 0);
    return;
  }

  lorawan_backend::begin();
  set_state("backend-selected");
  const auto lwb = lorawan_backend::runtime_info();
  Serial.printf("[lorawan] backend=%s region=%s version=%s fallback=%s lib=%d real=%d state=%s slot=%s code=%s err=%s\n",
                backend_string(),
                lorawan_cfg::REGION,
                lorawan_cfg::VERSION,
                lorawan_cfg::BACKEND_FALLBACK,
                lwb.library_available ? 1 : 0,
                lwb.real_backend_allowed ? 1 : 0,
                lwb.state,
                lwb.slot_state,
                lwb.readiness_code,
                lwb.error_code);

  if (runtime_config::join_autostart()) {
    request_join();
  }
}

void update() {
  if (!feature_flags::LORAWAN_FRAMEWORK_ENABLED) {
    return;
  }

  if (std::strcmp(g_state, "waiting-radio") == 0) {
    g_radio_ready = radio_sx1262::looks_alive();
    if (g_radio_ready) {
      lorawan_backend::begin();
      set_state("radio-ready");
      Serial.println(F("[lorawan] SX1262 now looks alive"));
      if (runtime_config::join_autostart() && g_secrets_ok) {
        request_join();
      }
    }
    return;
  }

  if (std::strcmp(g_state, "missing-secrets") == 0) {
    const auto key_status = lorawan_keys::validate_from_secrets();
    g_secrets_ok = secrets_present() && key_status.all_ok;
    if (g_secrets_ok) {
      lorawan_backend::begin();
      set_state("backend-selected");
      Serial.println(F("[lorawan] local OTAA secrets detected"));
      if (runtime_config::join_autostart()) {
        request_join();
      }
    }
    return;
  }

  lorawan_backend::update();
  const auto lwb = lorawan_backend::runtime_info();
  if (lwb.join_started && std::strcmp(g_state, "join-pending") == 0) {
    set_state(lwb.real_backend_allowed ? "join-real-slot" : "join-pending");
  }

  const uint32_t now = millis();
  if (g_join_requested && (now - g_last_retry_ms) >= cfg::LORAWAN_JOIN_TICK_MS) {
    g_last_retry_ms = now;
    if (cfg::PRINT_LORAWAN_DEBUG) {
      Serial.printf("[lorawan] join status backend=%s manager=%s backend_state=%s attempts=%lu code=%s err=%s radio=%s\n",
                    backend_string(),
                    g_state,
                    lwb.state,
                    static_cast<unsigned long>(lwb.join_attempts),
                    lwb.readiness_code,
                    lwb.error_code,
                    radio_sx1262::state_string());
    }
    // Auto-retry if not yet joined and backend allows it
    if (!lwb.joined && lorawan_backend::can_start_join()) {
      Serial.println(F("[lorawan] retrying join..."));
      (void)lorawan_backend::start_join();
    }
  }
}

const char* state_string() { return g_state; }
const char* backend_string() { return lorawan_backend::backend_name(); }

bool secrets_present() {
  const auto parsed = lorawan_keys::validate_from_secrets();
  return has_non_placeholder(secrets::lorawan_dev_eui()) &&
         has_non_placeholder(secrets::lorawan_join_eui()) &&
         has_non_placeholder(secrets::lorawan_app_key()) &&
         parsed.all_ok;
}

bool join_requested() { return g_join_requested; }

void request_join() {
  g_radio_ready = radio_sx1262::looks_alive();
  const auto key_status = lorawan_keys::validate_from_secrets();
  g_secrets_ok = secrets_present() && key_status.all_ok;
  if (!g_radio_ready) {
    set_state("waiting-radio");
    Serial.println(F("[lorawan] join request blocked: radio not ready"));
    return;
  }
  if (!g_secrets_ok) {
    set_state("missing-secrets");
    Serial.println(F("[lorawan] join request blocked: secrets missing"));
    return;
  }
  if (!lorawan_backend::can_start_join()) {
    set_state("backend-not-ready");
    Serial.printf("[lorawan] join request blocked: backend=%s state=%s code=%s err=%s\n",
                  backend_string(),
                  lorawan_backend::state_string(),
                  lorawan_backend::runtime_info().readiness_code,
                  lorawan_backend::runtime_info().error_code);
    return;
  }
  g_join_requested = true;
  g_last_retry_ms = 0;
  (void)lorawan_backend::start_join();
  set_state("join-pending");
  Serial.printf("[lorawan] join scaffold armed backend=%s\n", backend_string());
}

uint32_t join_attempts() { return lorawan_backend::runtime_info().join_attempts; }

RuntimeState runtime_state() {
  RuntimeState s{};
  s.join_attempts = join_attempts();
  s.last_action_ms = g_last_action_ms;
  s.radio_ready = g_radio_ready;
  s.secrets_ok = g_secrets_ok;
  s.join_requested = g_join_requested;
  return s;
}
}  // namespace lorawan_manager
