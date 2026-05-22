#pragma once

#include <Arduino.h>

namespace cfg {
static constexpr uint32_t SERIAL_BAUD = 115200;
static constexpr uint32_t GPS_BAUD = 9600;
static constexpr uint32_t STATUS_PRINT_MS = 1000;
static constexpr uint32_t APP_HEARTBEAT_MS = 5000;
static constexpr uint32_t MQTT_PUBLISH_MS = 5000;
static constexpr uint32_t WIFI_RETRY_MS = 10000;
static constexpr uint32_t MQTT_RETRY_MS = 5000;
static constexpr uint32_t GPS_SILENCE_WARNING_MS = 5000;
static constexpr uint32_t GPS_DRAIN_SLICE_MS = 20;
static constexpr uint32_t BOOT_BANNER_DELAY_MS = 200;
static constexpr uint32_t RADIO_SMOKE_TEST_DELAY_MS = 250;
static constexpr uint32_t SX1278_TX_TEST_TIMEOUT_MS = 1500;
static constexpr uint32_t SX1278_RX_TEST_TIMEOUT_MS = 1200;
// EU868 gateway duty cycle: 1% on 868 MHz band.
// JoinRequest ToA ~205ms @ SF9 → gateway blocked ~20s after each JoinAccept TX.
// Retry every 30s to let the gateway sub-band recover before the next attempt.
static constexpr uint32_t LORAWAN_JOIN_TICK_MS = 30000;
static constexpr uint32_t LORAWAN_BACKEND_JOIN_TICK_MS = 30000;
static constexpr int PPS_INTERRUPT_MODE = RISING;

static constexpr bool PRINT_GPS_RAW_NMEA = true;
static constexpr bool PRINT_PPS_EVENTS = true;
static constexpr bool PRINT_PIN_SUMMARY = true;
static constexpr bool PRINT_JSON_TELEMETRY = true;
static constexpr bool PRINT_RADIO_DEBUG = true;
static constexpr bool PRINT_LORAWAN_DEBUG = true;
static constexpr bool PRINT_LORAWAN_KEYS_STATUS = true;

static constexpr char DEVICE_ID[] = "xiao-proto1";
static constexpr char MQTT_BASE_TOPIC[] = "dogytag";

static constexpr uint8_t SX1278_REG_FIFO = 0x00;
static constexpr uint8_t SX1278_REG_OP_MODE = 0x01;
static constexpr uint8_t SX1278_REG_FRF_MSB = 0x06;
static constexpr uint8_t SX1278_REG_FRF_MID = 0x07;
static constexpr uint8_t SX1278_REG_FRF_LSB = 0x08;
static constexpr uint8_t SX1278_REG_PA_CONFIG = 0x09;
static constexpr uint8_t SX1278_REG_FIFO_ADDR_PTR = 0x0D;
static constexpr uint8_t SX1278_REG_FIFO_TX_BASE_ADDR = 0x0E;
static constexpr uint8_t SX1278_REG_FIFO_RX_BASE_ADDR = 0x0F;
static constexpr uint8_t SX1278_REG_IRQ_FLAGS = 0x12;
static constexpr uint8_t SX1278_REG_PAYLOAD_LENGTH = 0x22;
static constexpr uint8_t SX1278_REG_DIO_MAPPING_1 = 0x40;
static constexpr uint8_t SX1278_REG_VERSION = 0x42;
static constexpr uint8_t SX1278_EXPECTED_VERSION = 0x12;
static constexpr uint8_t SX1278_DIO0_TXDONE_MAPPING = 0x40;
static constexpr uint8_t SX1278_DIO0_RXDONE_MAPPING = 0x00;
static constexpr uint8_t SX1278_IRQ_TX_DONE_MASK = 0x08;
static constexpr uint8_t SX1278_IRQ_RX_DONE_MASK = 0x40;
static constexpr uint8_t SX1278_MODE_LONG_RANGE = 0x80;
static constexpr uint8_t SX1278_MODE_SLEEP = 0x00;
static constexpr uint8_t SX1278_MODE_STDBY = 0x01;
static constexpr uint8_t SX1278_MODE_TX = 0x03;
static constexpr uint8_t SX1278_MODE_RXCONTINUOUS = 0x05;
static constexpr uint8_t SX1278_TEST_PA_CONFIG = 0x8F;
static constexpr uint32_t SX1278_TEST_FREQUENCY_HZ = 433000000UL;
static constexpr uint8_t SX1278_TEST_FRF_MSB = 0x6C;
static constexpr uint8_t SX1278_TEST_FRF_MID = 0x40;
static constexpr uint8_t SX1278_TEST_FRF_LSB = 0x00;
static constexpr char SX1278_TEST_PAYLOAD[] = "smarttag beacon tx";
static constexpr size_t SX1278_TEST_PAYLOAD_MAX_LEN = 64;

static constexpr uint8_t SX1262_CMD_SET_STANDBY = 0x80;
static constexpr uint8_t SX1262_CMD_GET_STATUS = 0xC0;
static constexpr uint8_t SX1262_CMD_GET_ERRORS = 0x17;
static constexpr uint8_t SX1262_CMD_CLR_ERRORS = 0x07;
static constexpr uint8_t SX1262_EXPECTED_STATUS_MASK = 0xF0;
static constexpr uint8_t SX1262_STDBY_RC = 0x00;

static constexpr char LORAWAN_BACKEND_NAME[] = "RadioLib-adapter";
}  // namespace cfg
