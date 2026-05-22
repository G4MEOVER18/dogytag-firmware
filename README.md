# DogyTag Firmware

[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![PlatformIO](https://img.shields.io/badge/build-PlatformIO-orange.svg)](https://platformio.org)
[![LoRaWAN](https://img.shields.io/badge/LoRaWAN-1.0.3%20EU868-green.svg)](https://www.thethingsnetwork.org)
[![Frequenzen](https://img.shields.io/badge/Funk-868%20MHz%20%7C%20433%20MHz%20%7C%20BLE%20%7C%20WiFi-blue.svg)](#funk-technologien)

Multi-Radio-Tracking-System für Hunde und Haustiere — vollständig quelloffen, ohne Cloud-Zwang. Vier unabhängige Firmware-Komponenten bilden ein lückenloses Tracking-Netz aus LoRaWAN, bidirektionalem Raw-LoRa-Beacon-Protokoll (868 MHz), 433-MHz-Fallback, BLE-Geofencing und GPS-Präzisionslokalisierung.

> **Hardware-Sicherheitsforschungsprojekt** — alle RF-Frequenzen sind für den EU-ISM-Betrieb zugelassen (868 MHz / 433 MHz). Kein kommerzieller Dienst, kein Abo.

---

## Systemarchitektur

```
┌─────────────────────────────────────────────────────────────────┐
│                    DogyTag Tracking-System                      │
├─────────────────┬──────────────────┬───────────────────────────┤
│   Tag (Hund)    │     Gateway       │      Backend              │
├─────────────────┼──────────────────┼───────────────────────────┤
│  xiao-tag       │  heltec-gateway  │  TTN (The Things Network) │
│  (primär)       │                  │  ↓                        │
│  XIAO ESP32S3   │  HeltecTracker   │  MQTT Bridge              │
│  SX1262 868MHz  │  SX1262 868MHz   │  → lokaler Broker         │
│  SX1278 433MHz  │  CubeCell Modem  │  → Home-Automation        │
│  GPS NEO-M9N    │  MQTT → Broker   │                           │
├─────────────────┼──────────────────┼───────────────────────────┤
│  heltec-tag     │  cubecell-433    │                           │
│  (alternativ)   │  modem           │                           │
│  Heltec LoRa32  │  CubeCell AB02A  │                           │
│  SX1262 868MHz  │  ASR6502 SX1262  │                           │
│  CubeCell-Relay │  433 MHz UART    │                           │
│  BLE + GPS OLED │  Modem           │                           │
└─────────────────┴──────────────────┴───────────────────────────┘
```

**Kommunikationspfade:**

```
Tag  ──868MHz Beacon──►  Gateway  ──WiFi/MQTT──►  Broker  ──►  TTN
Tag  ◄──868MHz ACK──────  Gateway
Tag  ──433MHz Relay──────►  CubeCell-Modem  ──UART──►  Gateway
Tag  ◄──BLE Scan──  Smartphone  (Geofence-Erkennung)
Tag  ──GPS──►  (NEO-M9N / UC6580)  ──NMEA──►  Tag-Firmware
```

---

## Komponenten

| Verzeichnis | Hardware | Rolle | Version |
|---|---|---|---|
| [`xiao-tag/`](xiao-tag/) | XIAO ESP32S3 + Wio-SX1262 + SX1278 + GPS | Primäres Tracking-Tag (bidirektional, GPS, LoRaWAN) | v3.0.0 |
| [`heltec-tag/`](heltec-tag/) | Heltec WiFi LoRa 32 V3 + CubeCell + OLED | Alternatives Tag (BLE-Geofence, Dual-Radio, OLED) | v1.5.6 |
| [`heltec-gateway/`](heltec-gateway/) | Heltec Wireless Tracker v1.2 + CubeCell | NEAR_FIND-Gateway + MQTT-Bridge + TFT-Display | v3.0.0 |
| [`cubecell-433modem/`](cubecell-433modem/) | CubeCell HTCC-AB02A (ASR6502) | 433-MHz-UART-Slave-Modem (Relay-Fallback) | v3.0.0-modem |

---

## Funk-Technologien

Das System nutzt gleichzeitig vier unabhängige Funkschichten:

### 1. LoRaWAN 1.0.3 — EU868 (868 MHz)
- **Zweck**: Weitreichende Uplinks an TTN (The Things Network) über öffentliche LoRaWAN-Gateways
- **Aktivierung**: OTAA (Over-The-Air Activation) — kein hardgecodeter Session-Key
- **Payload**: 51 Byte max (Telemetrie: Akku, GPS, Status, Beacon-Zähler)
- **Komponente**: xiao-tag (SX1262), heltec-tag (SX1262)
- **Frequenzplan**: EU868 Multi-Channel (8 Kanäle, ADR aktivierbar)

### 2. Raw-LoRa Beacon-Protokoll — 868 MHz (NEAR_FIND)
- **Zweck**: Lokales Tracking ohne Internetverbindung — Gateway empfängt direkt
- **Parameter**: SF7, BW125 kHz, Sync-Word 0xAB, 14 dBm, 868.0 MHz
- **Bidirektional**: Tag sendet Beacon, wartet 600 ms auf ACK vom Gateway
- **ACK enthält**: RSSI und SNR → Tag weiß, wie stark das Signal ist
- **Pakettypen**: Beacon, ACK, LOG, GPS, CMD, RESP (siehe Protokoll-Tabelle)
- **Intervall**: TEST-Profil 5 s, Produktions-Profil 60 s

### 3. Raw-LoRa 433 MHz (Fallback / Redundanz)
- **Zweck**: Bessere Gebäudedurchdringung als 868 MHz, regionale ISM-Redundanz
- **Parameter**: SF7, BW125 kHz, Sync-Word 0xAB, 433.775 MHz
- **Hardware xiao-tag**: SX1278/Ra-01 direkt verdrahtet
- **Hardware heltec**: CubeCell HTCC-AB02A als UART-Slave-Modem
- **Modem-Protokoll**: ASCII-Textbefehle (`TX:<hex>`, `RX:<rssi>:<snr>:<hex>`)

### 4. BLE (Bluetooth Low Energy) — Geofence-Erkennung
- **Zweck**: Passive Näherungserkennung zum Smartphone des Besitzers
- **Bibliothek**: NimBLE (heltec-tag)
- **Funktionsweise**: Scan auf konfigurierbare Gerätenamen (bis zu 3, via CMD setzbar)
- **Timeout**: 10 Minuten ohne BLE-Sicht → erhöht Evidence-Score
- **Konfiguration**: `SET_BLE_GEOFENCE`-Befehl via Gateway

### 5. WiFi (Geofence-Erkennung)
- **Zweck**: Heimnetzwerk-SSID als Geofence-Marker (Tag ist zu Hause = kein Alarm)
- **Modus**: Asynchroner Scan, keine Verbindung, energiesparsam
- **Integration**: Teil des Evidence-basierten Zustandsautomaten

### 6. GPS (Präzisionslokalisierung)
- **Hardware xiao-tag**: u-blox NEO-M9N (UART, 9600 Baud) + PPS-Pin (GPIO3)
- **Hardware heltec-gateway**: UC6580 (on-board Heltec Tracker)
- **NMEA-Parsing**: Latitude/Longitude/Altitude/Satelliten/HDOP
- **Aktivierung**: Nur im LOST-/PRECISION-Zustand (Akku-Schonung)

### 7. MQTT (Backend-Bridge)
- **Gateway** veröffentlicht alle empfangenen Daten an lokalen Broker
- **Offline-Queue**: Bis zu 20 MQTT-Nachrichten gepuffert bei Netzwerkausfall
- **Topics** (Auswahl): `dogytag/<tag_id>/lora`, `/gps`, `/log`, `/ack`, `gateway/status`

---

## Paketformat-Referenz

Alle Raw-LoRa-Pakete verwenden Sync-Word `0xAB` zur Netzwerktrennung.

| Typ | Byte 0 | Bytes 1–6 | Bytes 7–8 | Bytes 9–10 | Byte 11 | Länge |
|---|---|---|---|---|---|---|
| **Beacon** | — | ID (6B ASCII) | Counter (u16 LE) | Akku_mV (u16 LE) | 0xAB | 11 B |
| **ACK** | 0x41 | Target-ID (6B) | RSSI (i8) | SNR×4 (i8) | — | 9 B |
| **LOG** | 0x4C | ID (6B) | Sequence (u16) | Akku_mV (u16) | 0xAB | 12 B |
| **GPS** | 0x47 | ID (6B) | Lat×1e6 (i32) | Lon×1e6 (i32) | Alt (i16), Sats, HDOP×10, 0xAB | 20 B |
| **CMD** | 0x43 | Target-ID (6B) | CmdID (u8), Flags (u8) | Seq (u16), ParamLen (u8) | Params (0–48B), CRC16 | 10–58 B |
| **RESP** | 0x52 | Source-ID (6B) | CmdID (u8), Result (u8) | Seq (u16), ParamLen (u8) | Params, CRC16 | 10–58 B |

CRC: CRC16-CCITT (Polynom 0x1021) über gesamten Payload ohne CRC-Bytes selbst.

---

## Zustandsmaschine (heltec-tag / xiao-tag)

```
DEEP_SLEEP ←──────────────────────────────────────────┐
     │ Aufwachen                                       │
     ▼                                                 │
  NORMAL ──── GW-Timeout + BLE-Miss + WiFi-Miss ──► LOST
     │              (Evidence ≥ 4)                     │
     │ Evidence ≥ 2                                    │
     ▼                                                 │
  CHECK ─────────────────────────────────────────► WARNING
     │                                            (→ LOST)
     │ CMD oder Trigger                               │
     ▼                                                │
 PRECISION ─── GPS-Fix + ACK ──────────────────► RECOVERY
  (GPS aktiv)                                    (→ NORMAL)
```

**Evidence-Score** (kumulativ):
- +2: Kein Gateway-ACK seit `gwTimeout_s`
- +1: BLE-Geräte nicht in Reichweite seit 10 Min
- +1: Home-WiFi-SSID nicht sichtbar

---

## Secrets & Konfiguration

**Wichtig: Niemals echte Schlüssel committen!**

Jede Komponente enthält eine `include/secrets.h` (oder `config/secrets.example.h`) mit `REPLACE_*`-Platzhaltern:

```cpp
// heltec-tag/include/secrets.h — ANPASSEN:
#define LORAWAN_DEVEUI   0xREPLACE_DEV_EUI_UINT64ULL
#define LORAWAN_APPKEY   { 0x00, 0x00, ... }  // 16 Bytes aus TTN Console
#define WIFI_SSID        "dein-heimnetzwerk"
#define MQTT_HOST        "192.168.x.x"
```

Für xiao-tag: `config/secrets.example.h` kopieren nach `config/secrets.local.h` (in .gitignore eingetragen).

---

## Voraussetzungen & Build

### Software
- [Visual Studio Code](https://code.visualstudio.com/) + [PlatformIO-Extension](https://platformio.org/install/ide?install=vscode)
- Oder PlatformIO Core CLI: `pip install platformio`

### Bibliotheken (automatisch via PlatformIO)
- RadioLib ≥ 6.6.0
- NimBLE-Arduino (heltec-tag)
- PubSubClient (heltec-gateway)
- TinyGPS++ (GPS-Parsing)
- Adafruit GFX + ST7735 (heltec-gateway Display)
- U8g2 (heltec-tag OLED)

### Build & Flash

```bash
# xiao-tag
cd xiao-tag
pio run -e xiao_esp32s3 -t upload

# heltec-tag
cd heltec-tag
pio run -t upload

# heltec-gateway
cd heltec-gateway
pio run -t upload

# cubecell-433modem
cd cubecell-433modem
pio run -t upload
```

---

## Hardware-Stückliste

### xiao-tag (Primär-Tag)
| Bauteil | Beschreibung |
|---|---|
| Seeed XIAO ESP32S3 | Hauptcontroller (dual-core 240 MHz, WiFi, BLE) |
| Wio-SX1262 Kit | LoRa 868 MHz (B2B-Connector, kein Löten) |
| Ra-01 / SX1278 | LoRa 433 MHz (Header-Pins gelötet) |
| u-blox NEO-M9N | GPS-Modul (UART, 9600 Baud, PPS) |
| LiPo 3.7V | Akku (Spannungsteiler an ADC) |

### heltec-gateway
| Bauteil | Beschreibung |
|---|---|
| Heltec Wireless Tracker v1.2 | ESP32-S3, SX1262, TFT 160×80, GPS UC6580 |
| CubeCell HTCC-AB02A | 433-MHz-Modem (UART2, 115200 Baud) |
| 5V USB-C Spannungsversorgung | Stationärer Betrieb |

### heltec-tag (Alternativ-Tag)
| Bauteil | Beschreibung |
|---|---|
| Heltec WiFi LoRa 32 V3 | ESP32-S3, SX1262, OLED 128×64 |
| CubeCell HTCC-AB02A | 433-MHz-Relay-Modem |
| u-blox NEO-M9N | GPS (UART1) |
| LiPo 3.7V | Akku |

### cubecell-433modem
| Bauteil | Beschreibung |
|---|---|
| Heltec CubeCell HTCC-AB02A | ASR6502 (Cortex-M0+), integriertes SX1262 |
| 433-MHz-Antenne | λ/4 Stabantenne oder Helix |

---

## Lizenz

MIT License — siehe [LICENSE](LICENSE)

Copyright (c) 2024–2026 G4MEOVER18

---

## Support / Donations

Wenn dieses Projekt deiner eigenen Forschung geholfen hat:

**Bitcoin:** `39vZWmnUwDReQ15BwqQXzyqVQ6U8LardEf`
