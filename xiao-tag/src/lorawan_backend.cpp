#include "lorawan_backend.h"

#include <cstring>

#include "feature_flags.h"
#include "lorawan_backend_slot.h"
#include "lorawan_backend_radiolib.h"
#include "lorawan_config.h"

namespace lorawan_backend {
void begin() {
  lorawan_backend_slot::begin();
  if (feature_flags::LORAWAN_BACKEND_RADIOLIB_SELECTED) {
    lorawan_backend_radiolib::begin();
  }
}

void update() {
  if (feature_flags::LORAWAN_BACKEND_RADIOLIB_SELECTED) {
    lorawan_backend_radiolib::update();
  }
}

bool can_start_join() {
  if (feature_flags::LORAWAN_BACKEND_RADIOLIB_SELECTED) {
    return lorawan_backend_radiolib::can_start_join();
  }
  return false;
}

bool start_join() {
  if (feature_flags::LORAWAN_BACKEND_RADIOLIB_SELECTED) {
    return lorawan_backend_radiolib::start_join();
  }
  return false;
}

int16_t send_uplink(uint8_t port, uint8_t* data, size_t len, bool confirmed) {
  if (feature_flags::LORAWAN_BACKEND_RADIOLIB_SELECTED) {
    return lorawan_backend_radiolib::send_uplink(port, data, len, confirmed);
  }
  return -1;
}

RuntimeInfo runtime_info() {
  RuntimeInfo info{};
  info.backend_name = backend_name();
  info.slot_header_available = lorawan_backend_slot::header_available();
  info.slot_object_scaffolded = lorawan_backend_slot::object_scaffolded();
  info.slot_state = lorawan_backend_slot::slot_state_string();

  if (feature_flags::LORAWAN_BACKEND_RADIOLIB_SELECTED) {
    info.configured = true;
    info.initialized = lorawan_backend_radiolib::initialized();
    info.join_supported = lorawan_backend_radiolib::join_supported();
    info.join_started = lorawan_backend_radiolib::join_attempts() > 0 ||
                        std::strcmp(lorawan_backend_radiolib::state_string(), "join-started-scaffold") == 0 ||
                        std::strcmp(lorawan_backend_radiolib::state_string(), "join-started-real-slot") == 0 ||
                        std::strcmp(lorawan_backend_radiolib::state_string(), "join-simulated-pending") == 0 ||
                        std::strcmp(lorawan_backend_radiolib::state_string(), "real-join-slot-pending") == 0;
    info.joined = lorawan_backend_radiolib::joined();
    info.library_available = lorawan_backend_radiolib::library_available();
    info.real_backend_allowed = lorawan_backend_radiolib::real_backend_allowed();
    info.join_attempts = lorawan_backend_radiolib::join_attempts();
    info.state = lorawan_backend_radiolib::state_string();
    info.readiness_code = lorawan_backend_radiolib::readiness_code();
    info.error_code = lorawan_backend_radiolib::error_code();
    return info;
  }

  info.state = "no-backend";
  info.readiness_code = "backend-not-selected";
  info.error_code = "none";
  return info;
}

const char* backend_name() {
  return feature_flags::LORAWAN_BACKEND_RADIOLIB_SELECTED ? lorawan_cfg::SELECTED_BACKEND : lorawan_cfg::BACKEND_FALLBACK;
}

const char* state_string() { return runtime_info().state; }
}  // namespace lorawan_backend
