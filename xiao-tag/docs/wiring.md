# XIAO ESP32S3 Wiring — SmartTag Proto 1

**Board:** Seeed Studio XIAO ESP32S3 + Wio-SX1262 Kit (B2B)
**Firmware:** v3.0.0
**Date:** 2026-03-24

---

## Pin Assignment

| Component | Signal | XIAO Label | GPIO | Notes |
|-----------|--------|------------|------|-------|
| **Wio-SX1262 (EU868)** | NSS / CS | — | GPIO41 | B2B pad **MTDI** — no soldering |
| | DIO1 | — | GPIO39 | B2B pad **MTCK** — no soldering |
| | RESET | — | GPIO42 | B2B pad **MTMS** — no soldering |
| | BUSY | — | GPIO40 | B2B pad **MTDO** — no soldering |
| | RF_SW | — | GPIO38 | B2B pad — no soldering |
| | SCK | D8 | GPIO7 | Shared SPI bus (header pin) |
| | MISO | D9 | GPIO8 | Shared SPI bus (header pin) |
| | MOSI | D10 | GPIO9 | Shared SPI bus (header pin) |
| **Ra-01 (SX1278, 433 MHz)** | NSS / CS | D5 | GPIO6 | Header pin |
| | RESET | D0 | GPIO1 | Header pin |
| | DIO0 | D1 | GPIO2 | Header pin |
| | SCK | D8 | GPIO7 | Shared SPI bus (header pin) |
| | MISO | D9 | GPIO8 | Shared SPI bus (header pin) |
| | MOSI | D10 | GPIO9 | Shared SPI bus (header pin) |
| **GPS (u-blox)** | GPS TX → XIAO RX | D7 | GPIO44 | UART RX on XIAO (header pin) |
| | GPS RX ← XIAO TX | D6 | GPIO43 | UART TX from XIAO (header pin) |
| | PPS | D2 | GPIO3 | Header pin |

---

## B2B Connector (Wio-SX1262)

The Wio-SX1262 module connects to the XIAO ESP32S3 via the B2B (Board-to-Board) connector on the underside.
**No soldering required.** GPIO38–42 are the back pads routed through this connector.

| B2B Pad | GPIO | SX1262 Signal |
|---------|------|---------------|
| MTDI | GPIO41 | NSS (Chip Select) |
| MTCK | GPIO39 | DIO1 (IRQ) |
| MTMS | GPIO42 | RESET |
| MTDO | GPIO40 | BUSY |
| — | GPIO38 | RF_SW (antenna switch) |

---

## Shared SPI Bus

Ra-01 and Wio-SX1262 share the hardware SPI bus (VSPI, pins D8/D9/D10).
**NSS pins must never be LOW simultaneously.**
Software ensures mutual exclusion via separate NSS GPIO control.

---

## LoRaWAN

- SX1262 (Wio-SX1262) handles LoRaWAN EU868 via RadioLib 7.6.0
- SX1278 (Ra-01) handles 433 MHz direct LoRa (beacon / smoke tests)
- TTN App: `dogytag-v1` | Device: `xiao-proto1`
- Region: EU863-870 | LoRaWAN 1.0.3 Rev A | OTAA

---

## GPS

- Module: u-blox (9600 baud)
- UART2 on ESP32S3
- PPS signal on GPIO3 (D2, header pin) — interrupt-driven, rising edge
