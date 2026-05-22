#pragma once
#include <Arduino.h>
namespace lorawan_cfg {
static constexpr char REGION[] = "EU863-870";
static constexpr char VERSION[] = "LoRaWAN 1.0.3";
static constexpr char RP_VERSION[] = "RP001 1.0.3 rev A";
static constexpr bool USE_OTAA = true;
static constexpr uint32_t JOIN_RETRY_MS = 15000;
static constexpr char SELECTED_BACKEND[] = "RadioLib-adapter";
static constexpr char BACKEND_FALLBACK[] = "Semtech-Basics-Modem-evaluate";
static constexpr char BACKEND_NOTE[] = "Adapter supports staged migration from scaffold to real RadioLib integration for SX1262";
static constexpr char RADIOLIB_ENV_NAME[] = "xiao_esp32s3_radiolib";
}
