#pragma once
#include <Arduino.h>

// =============================================================
// pins.h — SmartTag XIAO ESP32S3 v3.0.0
// Board: Seeed XIAO ESP32S3 + Wio-SX1262 Kit (B2B)
// External: Ra-01 (SX1278, 433 MHz) + GPS (u-blox)
//
// VERIFIED: Wio-SX1262 B2B uses GPIO38-42 (back pads, no soldering)
// Source: DogyTag_LMIC_XIAO-ESP32S3_Wio-SX1262_FIXED reference project
// =============================================================

// Shared SPI Bus (Hardware VSPI on XIAO)
// D8=GPIO7(SCK), D9=GPIO8(MISO), D10=GPIO9(MOSI)
static constexpr gpio_num_t PIN_SPI_SCK  = GPIO_NUM_7;   // D8
static constexpr gpio_num_t PIN_SPI_MISO = GPIO_NUM_8;   // D9
static constexpr gpio_num_t PIN_SPI_MOSI = GPIO_NUM_9;   // D10

// SX1262 — Wio-SX1262 via B2B connector (EU868 LoRaWAN)
// Connected via internal B2B pads (GPIO38-42), NO soldering required
static constexpr gpio_num_t PIN_SX1262_NSS   = GPIO_NUM_41;  // JTAG MTDI  — B2B
static constexpr gpio_num_t PIN_SX1262_DIO1  = GPIO_NUM_39;  // JTAG MTCK  — B2B
static constexpr gpio_num_t PIN_SX1262_RST   = GPIO_NUM_42;  // JTAG MTMS  — B2B
static constexpr gpio_num_t PIN_SX1262_BUSY  = GPIO_NUM_40;  // JTAG MTDO  — B2B
static constexpr gpio_num_t PIN_SX1262_RF_SW = GPIO_NUM_38;  // internal   — B2B

// SX1278 / Ra-01 (433 MHz, external module)
// All on accessible header pins D0-D5
static constexpr gpio_num_t PIN_SX1278_NSS  = GPIO_NUM_6;   // D5 — header pin
static constexpr gpio_num_t PIN_SX1278_RST  = GPIO_NUM_1;   // D0 — header pin
static constexpr gpio_num_t PIN_SX1278_DIO0 = GPIO_NUM_2;   // D1 — header pin

// GPS Module (UART2, 9600 baud)
static constexpr gpio_num_t PIN_GPS_TX  = GPIO_NUM_43;  // D6 — UART TX to GPS RX
static constexpr gpio_num_t PIN_GPS_RX  = GPIO_NUM_44;  // D7 — UART RX from GPS TX
static constexpr gpio_num_t PIN_GPS_PPS = GPIO_NUM_3;   // D2 — header pin

// =============================================================
// WIRING SUMMARY
// =============================================================
// Wio-SX1262 (EU868 LoRaWAN) — B2B connector (no soldering):
//   NSS   → GPIO41 (MTDI)  — internal B2B
//   DIO1  → GPIO39 (MTCK)  — internal B2B
//   RST   → GPIO42 (MTMS)  — internal B2B
//   BUSY  → GPIO40 (MTDO)  — internal B2B
//   RF_SW → GPIO38         — internal B2B
//   SCK   → D8 (GPIO7)     — shared SPI header
//   MISO  → D9 (GPIO8)     — shared SPI header
//   MOSI  → D10 (GPIO9)    — shared SPI header
//
// Ra-01 (SX1278, 433 MHz) — header pins (solder to XIAO header):
//   NSS   → D5 (GPIO6)    — solder to header
//   RST   → D0 (GPIO1)    — solder to header
//   DIO0  → D1 (GPIO2)    — solder to header
//   SCK   → D8 (GPIO7)    — shared SPI header
//   MISO  → D9 (GPIO8)    — shared SPI header
//   MOSI  → D10 (GPIO9)   — shared SPI header
//   3.3V  → 3V3
//   GND   → GND
//
// GPS (u-blox, 9600 baud) — header pins (solder to XIAO header):
//   TX    → D7 (GPIO44)   — GPS TX to XIAO RX
//   RX    → D6 (GPIO43)   — GPS RX from XIAO TX
//   PPS   → D2 (GPIO3)    — header pin
//   VCC   → 3V3
//   GND   → GND
// =============================================================
