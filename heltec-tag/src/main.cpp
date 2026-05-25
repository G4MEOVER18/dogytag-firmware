// =============================================================
// SmartTag HeltecLoRa32 V3 – Firmware v1.5.6
// Heltec WiFi LoRa 32 v3 (SX1262, 868 MHz EU)
// TTN Application: dogytag-v1 | Device: heltec-lora32v3-tag
// LoRaWAN 1.0.3 OTAA | EU868 | RadioLib 6.6.0
// MAC: ac:a7:04:09:43:f0 | DevEUI: ACA704FFFE0943F0
//
// Dual-Mode:
//   - LoRaWAN OTAA → TTN alle 30s (NORMAL tracking)
//   - Raw LoRa Beacon → 868.0 MHz SF7 BW125 alle 5s (NEAR_FIND)
//
// v1.2.0-bidir: Bidirektionale LoRa-Kommunikation
//   - Nach Beacon TX: 600ms RX-Fenster auf Gateway-ACK warten
//   - ACK enthält RSSI (wie gut GW das Signal empfangen hat)
//   - Wenn GW in Range: LOG-Paket mit Batterie + Seq senden
//   - OLED zeigt GW-Status (RSSI, ACK-Count)
//   - GW-Timeout nach 60s ohne ACK
//
// Paket-Typen:
//   Beacon: [ID 6B][CNT 2B][BAT 2B][0xAB]          = 11 Bytes (TX)
//   ACK:    [0x41][TARGET 6B][RSSI i8][SNR*4 i8]    =  9 Bytes (RX)
//   LOG:    [0x4C][ID 6B][SEQ 2B][BAT 2B][0xAB]     = 12 Bytes (TX)
//
// v1.3.0-433: CubeCell HTCC-AB02A als 433MHz Modem (UART2)
//   - Nach Beacon TX: Paket auch auf 433MHz senden (via CubeCell)
//   - CubeCell empfangene Pakete auf OLED anzeigen
//   - Pins: GPIO4=RX2 (←CubeCell TX1), GPIO5=TX2 (→CubeCell RX1)
//
// WiFi/MQTT vorbereitet – noch deaktiviert (WIFI_ENABLED 0)
// =============================================================

#define WIFI_ENABLED  0

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <Preferences.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <RadioLib.h>
#include <TinyGPS++.h>
#include <esp_sleep.h>
#include <NimBLEDevice.h>
#include <WiFi.h>   // immer inkl. für WiFi-Scan (Geofence-Evidenz)
#if WIFI_ENABLED
#  include <PubSubClient.h>
#  include <ArduinoJson.h>
#endif
#include "secrets.h"
#include "config.h"
#include "command_ids.h"

// --- OLED (Heltec v3: SDA=17, SCL=18, RST=21) ---
constexpr uint8_t OLED_SDA  = 17;
constexpr uint8_t OLED_SCL  = 18;
constexpr uint8_t OLED_ADDR = 0x3C;
constexpr uint8_t OLED_RST  = 21;

// --- Power Control ---
constexpr uint8_t VEXT_CTRL = 36;

// --- LED ---
constexpr uint8_t STATUS_LED = 35;

// --- Battery ADC ---
constexpr uint8_t VBAT_ADC_PIN = 1;

// --- SX1262 Pins ---
constexpr uint8_t LORA_NSS  = 8;
constexpr uint8_t LORA_DIO1 = 14;
constexpr uint8_t LORA_RST  = 12;
constexpr uint8_t LORA_BUSY = 13;

// --- Timing ---
constexpr uint32_t UPLINK_INTERVAL_MS  = 30000;

// --- Raw LoRa Parameter (Defaults, überschrieben durch cfg) ---
constexpr uint8_t  BEACON_SYNC_WORD  = 0xAB;

// --- Bidirektional 868MHz ---
constexpr uint8_t  PKT_ACK           = 0x41;
constexpr uint8_t  PKT_LOG           = 0x4C;

// --- 433MHz Modem (CubeCell via UART2) ---
constexpr uint8_t  MODEM433_RX_PIN   = 4;   // GPIO4 ← CubeCell TX1
constexpr uint8_t  MODEM433_TX_PIN   = 5;   // GPIO5 → CubeCell RX1
constexpr uint32_t MODEM433_BAUD     = 115200;

HardwareSerial serial433(2);

static char    m433Buf[160];
static uint8_t m433Pos   = 0;
static bool     m433Ready     = false;
static int16_t  last433Rssi   = 0;
static uint32_t rx433Count    = 0;
static uint32_t tx433Count    = 0;
static uint32_t last433PingMs = 0;

static void bytesToHex433(const uint8_t* d, size_t len, char* out) {
    for (size_t i = 0; i < len; i++) sprintf(out + i*2, "%02X", d[i]);
    out[len*2] = '\0';
}

// Paket über 433MHz-Modem senden (nur wenn Modem bereit)
void relay433(const uint8_t* data, size_t len) {
    if (!m433Ready) return;
    char hex[129];
    bytesToHex433(data, len, hex);
    serial433.printf("TX:%s\n", hex);
    tx433Count++;
}

// Eingehende RX-Zeile vom CubeCell verarbeiten
void handle433Rx(const char* line) {
    // Format: RX:<rssi>:<snr>:<hexstring>
    const char* p  = line + 3;
    int rssi = atoi(p);
    const char* p2 = strchr(p, ':');
    if (!p2) return;
    const char* p3 = strchr(p2+1, ':');
    if (!p3) return;
    last433Rssi = (int16_t)rssi;
    rx433Count++;
    Serial.printf("[433] RX RSSI=%d len=%u\n", rssi, (unsigned)strlen(p3+1)/2);
}

// Serial2 pollen
void poll433() {
    while (serial433.available()) {
        char c = (char)serial433.read();
        if (c == '\n' || c == '\r') {
            if (m433Pos > 0) {
                m433Buf[m433Pos] = '\0';
                    if (strcmp(m433Buf, "READY") == 0 ||
                    strncmp(m433Buf, "STATUS:", 7) == 0) {
                    m433Ready = true;
                    serial433.println("OK");
                    Serial.println("[433] Modem bereit");
                } else if (strncmp(m433Buf, "RX:", 3) == 0) {
                    handle433Rx(m433Buf);
                }
                m433Pos = 0;
            }
        } else if (m433Pos < sizeof(m433Buf)-1) {
            m433Buf[m433Pos++] = c;
        }
    }
}

// =============================================================
// GPS (u-blox NEO-M9N via UART1)
// Verdrahtung:
//   NEO-M9N TX  →  GPIO19 (ESP32 RX)
//   NEO-M9N RX  →  GPIO20 (ESP32 TX, optional)
//   NEO-M9N VCC →  3.3V
//   NEO-M9N GND →  GND
// Hinweis: GPIO19/20 sind auf dem Heltec V3 Header frei verfügbar.
//          Die anderen freien Optionen wären GPIO46/47.
// =============================================================
constexpr uint8_t  GPS_RX_PIN = 19;   // ← NEO-M9N TX
constexpr uint8_t  GPS_TX_PIN = 20;   // → NEO-M9N RX (optional, für UBX-Konfiguration)
constexpr uint32_t GPS_BAUD   = 9600; // NEO-M9N default (ggf. 38400 je nach Modul-Konfig)

HardwareSerial serialGps(1);   // UART1
TinyGPSPlus    gps;

struct GpsFix {
    double   lat;
    double   lon;
    float    altM;
    uint8_t  sats;
    float    hdop;
    bool     valid;
    uint32_t ageMs;    // millis() beim letzten Fix
};
static GpsFix lastGpsFix = {};

static bool hasGpsFix() {
    return lastGpsFix.valid && (millis() - lastGpsFix.ageMs < 120000UL);
}

static void pollGps() {
    while (serialGps.available()) {
        if (gps.encode(serialGps.read())) {
            if (gps.location.isValid() && gps.location.age() < 3000) {
                lastGpsFix.lat   = gps.location.lat();
                lastGpsFix.lon   = gps.location.lng();
                lastGpsFix.altM  = gps.altitude.isValid() ? (float)gps.altitude.meters() : 0.0f;
                lastGpsFix.sats  = gps.satellites.isValid() ? (uint8_t)gps.satellites.value() : 0;
                lastGpsFix.hdop  = gps.hdop.isValid() ? (float)gps.hdop.hdop() : 99.9f;
                lastGpsFix.valid = true;
                lastGpsFix.ageMs = millis();
            }
        }
    }
}

// =============================================================
// BLE GEOFENCE (NimBLE, passiv/aktiv Scan auf Abruf)
// Geofence-Auslöser: Gerätename in cfg.bleGeofenceNames
// Test-Phase: nur Serial + OLED-Anzeige, kein State-Wechsel
// Timeout: 10 Minuten ohne Sicht → Alert
// =============================================================
constexpr uint32_t BLE_GEO_TIMEOUT_MS = 10UL * 60UL * 1000UL; // 10 min

// Geofence-Targets (Kopie aus cfg, vor jedem Scan aktualisiert)
static char    bleGeoTargets[3][32] = {};
static uint8_t bleGeoTargetCnt      = 0;

// Scan- und Geofence-Zustand
static volatile uint32_t bleGeoLastSeenMs = 0;   // millis() letzter Fund
static volatile bool     bleGeoInZone     = false;
static bool              bleGeoAlertFired = false;
static bool              bleScanRunning   = false;
static uint32_t          bleNextScanMs    = 0;
static bool              bleInitDone      = false;

// NimBLE-Callback – läuft im BT-Task, nur volatile Vars schreiben
class BleGeoScanCb : public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* dev) override {
        if (!dev->haveName()) return;
        const char* name = dev->getName().c_str();
        for (int i = 0; i < bleGeoTargetCnt; i++) {
            if (bleGeoTargets[i][0] && strcmp(name, bleGeoTargets[i]) == 0) {
                bleGeoLastSeenMs = (uint32_t)millis();
                bleGeoInZone     = true;
                // bleGeoAlertFired wird im main-task zurückgesetzt
                Serial.printf("[BLE-GEO] '%s' in Sicht! RSSI=%d\n",
                              name, dev->getRSSI());
                return;
            }
        }
    }
};
static BleGeoScanCb bleGeoCb;

// =============================================================
// WIFI GEOFENCE SCAN – Globals (Funktionen nach cfg-Deklaration)
// =============================================================
static bool     wifiInZone        = false;   // Home-SSID in Sicht
static bool     wifiScanRunning_g = false;   // Scan aktiv
static bool     wifiScanDone      = false;   // Mindestens 1 Scan abgeschlossen
static uint32_t wifiNextScanMs    = 0;

// =============================================================
// RTC-MEMORY – überlebt Deep Sleep
// =============================================================
RTC_DATA_ATTR static uint32_t rtcBootCount   = 0;   // Anzahl Wakeups
RTC_DATA_ATTR static uint32_t rtcBeaconCount = 0;
RTC_DATA_ATTR static uint32_t rtcAckCount    = 0;
RTC_DATA_ATTR static uint32_t rtcLogSeq      = 0;
RTC_DATA_ATTR static uint32_t rtcUplinkCount = 0;
RTC_DATA_ATTR static uint8_t  rtcState       = 1;   // TrackerState::NORMAL
RTC_DATA_ATTR static bool     rtcJoined      = false;
RTC_DATA_ATTR static bool     rtcGwInRange   = false;
RTC_DATA_ATTR static int8_t   rtcLastGwRssi  = 0;

// =============================================================
// GLOBALE OBJEKTE
// =============================================================
Adafruit_SSD1306 display(128, 64, &Wire, -1);
SX1262*      radio = nullptr;
LoRaWANNode* node  = nullptr;
Preferences  prefs;

// --- State ---
bool     joined         = false;
uint32_t lastUplink     = 0;
uint32_t lastBeacon     = 0;
uint32_t lastBlink      = 0;
bool     ledState       = false;
uint32_t uplinkCount    = 0;
uint32_t beaconCount    = 0;
String   lastStatus     = "–";
uint32_t lastHeartbeat  = 0;
uint32_t joinRetryCount = 0;

// --- Bidirektional State ---
bool     gwInRange   = false;
uint32_t lastAckTime = 0;
int8_t   lastGwRssi  = 0;
float    lastGwSnr   = 0.0f;
uint32_t ackCount    = 0;
uint32_t logSeq      = 0;

// --- Laufzeit-Config & State Machine ---
TrackerConfig    cfg;
TrackerState     currentState    = TrackerState::NORMAL;
uint32_t         lastCheckMs     = 0;
uint32_t         lastGwSeenMs    = 0;     // letztes erfolgreiches ACK
uint8_t          checkFailCount  = 0;
uint8_t          warningCycles   = 0;
bool             configDirty     = false;
static uint16_t  cmdSeqOut       = 0;     // ausgehende CMD-Sequenznummer

// --- ACK RX IRQ (SX126x available() funktioniert nur mit attachiertem Handler) ---
volatile bool ackPacketReady = false;
void IRAM_ATTR onAckPacket() { ackPacketReady = true; }

// =============================================================
// DISPLAY
// =============================================================
void drawStatus(const char* l1, const char* l2, const char* l3 = nullptr)
{
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println(F(FIRMWARE_VERSION));
    display.drawFastHLine(0, 10, 128, SSD1306_WHITE);
    display.setCursor(0, 14);
    display.println(l1);
    display.setCursor(0, 26);
    display.println(l2);
    if (l3) {
        display.setCursor(0, 38);
        display.println(l3);
    }
    // GW + 433 Status + BLE Alert (Zeile y=52, Font 8px, max bis y=63)
    display.setCursor(0, 52);
    char gwbuf[22];
    if (gwInRange) {
        snprintf(gwbuf, sizeof(gwbuf), "GW:%ddBm%s ACK:%lu",
                 lastGwRssi, bleGeoAlertFired ? "!" : " ", ackCount);
    } else {
        snprintf(gwbuf, sizeof(gwbuf), "GW:-- 433:%s%s",
                 m433Ready ? "OK" : "--", bleGeoAlertFired ? " !GEO" : "");
    }
    display.println(gwbuf);
    display.display();
}

// =============================================================
// BATTERIE
// =============================================================
uint16_t readBatMv()
{
    uint16_t adcRaw = analogRead(VBAT_ADC_PIN);
    return (uint16_t)((uint32_t)adcRaw * 2 * 3300 / 4095);
}

// =============================================================
// ACK VOM GATEWAY PARSEN
// Format: [0x41][TARGET_ID 6B][RSSI i8][SNR*4 i8] = 9 Bytes
// =============================================================
void parseAck(const uint8_t* data, size_t len)
{
    if (len < 9) return;
    if (data[0] != PKT_ACK) return;

    // Ziel-ID prüfen
    char targetId[8] = {0};
    for (int i = 0; i < 6; i++) {
        if (data[1 + i] == 0) break;
        targetId[i] = (char)data[1 + i];
    }
    if (strncmp(targetId, cfg.deviceId, 6) != 0) return;  // nicht für uns

    lastGwRssi  = (int8_t)data[7];
    lastGwSnr   = (float)((int8_t)data[8]) / 4.0f;
    gwInRange   = true;
    lastAckTime = millis();
    ackCount++;

    Serial.printf("[ACK] GW sieht uns: RSSI=%d SNR=%.1f  ACK#%lu\n",
                  lastGwRssi, lastGwSnr, ackCount);
}

// =============================================================
// LOG-PAKET AN GATEWAY SENDEN
// Format: [0x4C][ID 6B][SEQ 2B][BAT 2B][0xAB] = 12 Bytes
// =============================================================
void sendLogPacket()
{
    logSeq++;
    uint16_t batMv = readBatMv();

    uint8_t pkt[12] = {0};
    pkt[0] = PKT_LOG;
    const char* tid = cfg.deviceId;
    for (int i = 0; i < 6 && tid[i]; i++) pkt[1 + i] = (uint8_t)tid[i];
    pkt[7]  = (logSeq >> 8) & 0xFF;
    pkt[8]  = logSeq & 0xFF;
    pkt[9]  = (batMv >> 8) & 0xFF;
    pkt[10] = batMv & 0xFF;
    pkt[11] = 0xAB;

    int16_t rc = radio->transmit(pkt, sizeof(pkt));
    if (rc == RADIOLIB_ERR_NONE) {
        Serial.printf("[LOG] #%lu bat=%dmV OK\n", logSeq, batMv);
        if (cfg.lora433Enabled) relay433(pkt, sizeof(pkt));
    } else {
        Serial.printf("[LOG] Fehler: %d\n", rc);
    }
}

// =============================================================
// BLE GEOFENCE FUNKTIONEN
// =============================================================
static void initBle() {
    if (bleInitDone) return;
    NimBLEDevice::init("");
    NimBLEDevice::setPower(ESP_PWR_LVL_N0);  // minimale Sendeleistung für Scan
    bleInitDone  = true;
    bleNextScanMs = millis() + 5000UL;  // erster Scan nach 5s
    Serial.printf("[BLE] NimBLE init – %d Geofence-Targets konfiguriert\n",
                  cfg.bleGeofenceCount);
    for (int i = 0; i < cfg.bleGeofenceCount && i < 3; i++) {
        if (cfg.bleGeofenceNames[i][0])
            Serial.printf("[BLE] Target[%d] = '%s'\n", i, cfg.bleGeofenceNames[i]);
    }
}

static void startBleScan() {
    if (!cfg.bleEnabled || !bleInitDone || bleScanRunning) return;
    // Targets aus cfg kopieren (thread-safe: wird vor Scan-Start gesetzt)
    bleGeoTargetCnt = min((uint8_t)3, cfg.bleGeofenceCount);
    for (int i = 0; i < bleGeoTargetCnt; i++) {
        strncpy(bleGeoTargets[i], cfg.bleGeofenceNames[i], 31);
        bleGeoTargets[i][31] = '\0';
    }
    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setAdvertisedDeviceCallbacks(&bleGeoCb, false);
    scan->setActiveScan(true);   // aktiv = Scan-Response (enthält Gerätename)
    scan->setInterval(100);
    scan->setWindow(99);
    uint32_t durSec = max(1u, (unsigned)(cfg.bleScanDuration_ms / 1000));
    scan->start((uint32_t)durSec, false);  // non-blocking
    bleScanRunning = true;
    Serial.printf("[BLE] Scan gestartet: %us, %d Targets\n", durSec, bleGeoTargetCnt);
}

static void pollBleScan() {
    if (!cfg.bleEnabled || !bleInitDone) return;
    uint32_t now = millis();

    // Scan fertig?
    if (bleScanRunning && !NimBLEDevice::getScan()->isScanning()) {
        bleScanRunning = false;
        bleNextScanMs  = now + (uint32_t)cfg.wakeInterval_s * 1000UL;
        Serial.printf("[BLE] Scan fertig – InZone=%s letzteSicht=%lus\n",
                      bleGeoInZone ? "JA" : "NEIN",
                      bleGeoLastSeenMs ? (now - bleGeoLastSeenMs) / 1000UL : 9999UL);

        // NEAR_FIND: Owner-Gerät im LOST-Mode gesehen → RECOVERY + LED-Flash
        if (bleGeoInZone && (currentState == TrackerState::LOST ||
                              currentState == TrackerState::WARNING)) {
            Serial.println("[BLE-GEO] *** NEAR_FIND: Owner-Gerät in LOST-State gesehen! ***");
            // 3x schnelles LED-Blinken als Bestätigung
            for (int i = 0; i < 6; i++) {
                digitalWrite(STATUS_LED, i % 2);
                delay(100);
            }
            currentState = TrackerState::RECOVERY;
            Serial.println("[SM] LOST/WARNING → RECOVERY (NEAR_FIND via BLE)");
        }

        // Alert ggf. zurücksetzen wenn Gerät wieder da
        if (bleGeoInZone && bleGeoAlertFired) {
            bleGeoAlertFired = false;
            Serial.printf("[BLE-GEO] Gerät wieder in Sicht – Alert zurückgesetzt\n");
        }
        bleGeoInZone = false;  // Reset: nächster Scan setzt es erneut
    }

    // Geofence-Timeout prüfen
    if (cfg.bleGeofenceEnabled && bleGeoLastSeenMs > 0 && !bleGeoAlertFired) {
        if (now - (uint32_t)bleGeoLastSeenMs > BLE_GEO_TIMEOUT_MS) {
            bleGeoAlertFired = true;
            uint32_t minAway = (now - (uint32_t)bleGeoLastSeenMs) / 60000UL;
            Serial.printf("[BLE-GEO] *** ALERT: Geofence-Target seit %lu min nicht gesehen! ***\n",
                          minAway);
            // TEST-Modus: nur loggen (kein State-Wechsel)
            // PROD-Modus: CHECK/WARNING triggern
            if (cfg.profile != PowerProfile::TEST) {
                // Produktion: State-Übergang anstoßen
                if (currentState == TrackerState::NORMAL ||
                    currentState == TrackerState::CHECK) {
                    checkFailCount = cfg.checkFailsForWarning;  // sofort WARNING
                }
            }
        }
    }

    // Nächsten Scan anstoßen
    if (!bleScanRunning && now >= bleNextScanMs) {
        startBleScan();
    }
}

// =============================================================
// CONFIG PERSISTENZ (NVS via Preferences)
// Eigener Namespace "tracker_cfg" – unabhängig von LoRaWAN-Prefs
// =============================================================
static void saveConfig() {
    Preferences cfgPrefs;
    cfgPrefs.begin("tracker_cfg", false);
    cfg.configVersion++;
    cfgPrefs.putBytes("cfg", &cfg, sizeof(TrackerConfig));
    cfgPrefs.end();
    configDirty = false;
    Serial.printf("[CFG] Gespeichert v%u (%u Bytes)\n",
                  cfg.configVersion, (unsigned)sizeof(TrackerConfig));
}

static bool loadConfig() {
    Preferences cfgPrefs;
    cfgPrefs.begin("tracker_cfg", true);
    size_t len = cfgPrefs.getBytes("cfg", &cfg, sizeof(TrackerConfig));
    cfgPrefs.end();
    if (len == sizeof(TrackerConfig)) {
        Serial.printf("[CFG] Geladen v%u\n", cfg.configVersion);
        return true;
    }
    cfg = TrackerConfig();
    Serial.println("[CFG] Keine Config – nutze Defaults");
    return false;
}

// =============================================================
// WIFI GEOFENCE SCAN (Scan-Only, kein Connect)
// Sucht nach cfg.wifiSsid / cfg.wifiGeofenceSsid als Heimindikator.
// Läuft asynchron, startet nach BLE-Scan um 2.4GHz-Kollision zu vermeiden.
// =============================================================
static void startWifiScanGeo() {
    if (!cfg.wifiEnabled) return;
    if (bleScanRunning) return;          // BLE hat Vorrang auf 2.4GHz
    if (wifiScanRunning_g) return;
    if (WiFi.getMode() == WIFI_OFF) WiFi.mode(WIFI_STA);
    int rc = WiFi.scanNetworks(true);    // async=true
    if (rc == WIFI_SCAN_FAILED) return;
    wifiScanRunning_g = true;
    Serial.println("[WiFi-GEO] Scan gestartet...");
}

static void pollWifiScanGeo() {
    if (!cfg.wifiEnabled) return;
    uint32_t now = millis();

    // Nächsten Scan anstoßen wenn fällig
    if (!wifiScanRunning_g && !bleScanRunning && wifiNextScanMs > 0
        && now >= wifiNextScanMs) {
        startWifiScanGeo();
    }
    if (!wifiScanRunning_g) return;

    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) return;

    wifiScanRunning_g = false;
    wifiScanDone      = true;
    wifiNextScanMs    = now + (uint32_t)cfg.wakeInterval_s * 1000UL;

    if (n <= 0) {
        wifiInZone = false;
        Serial.printf("[WiFi-GEO] Scan fertig: keine Netzwerke / Fehler (%d)\n", n);
        return;
    }

    wifiInZone = false;
    for (int i = 0; i < n; i++) {
        const char* ssid = WiFi.SSID(i).c_str();
        bool matchHome = cfg.wifiSsid[0] &&
                         strcmp(ssid, cfg.wifiSsid) == 0;
        bool matchGeo  = cfg.wifiGeofenceEnabled && cfg.wifiGeofenceSsid[0] &&
                         strcmp(ssid, cfg.wifiGeofenceSsid) == 0;
        if (matchHome || matchGeo) {
            wifiInZone = true;
            Serial.printf("[WiFi-GEO] '%s' in Sicht! RSSI=%d\n",
                          ssid, WiFi.RSSI(i));
            break;
        }
    }
    WiFi.scanDelete();
    Serial.printf("[WiFi-GEO] Scan fertig: %d Netzwerke, InZone=%s\n",
                  n, wifiInZone ? "JA" : "NEIN");
}

// =============================================================
// EVIDENCE SCORE (Konzept: mehrstufige Geofence-Entscheidung)
//
// Score-Beiträge:
//   +2  Gateway nicht in Range (seit > gwTimeout_s)
//   +1  BLE-Geofence-Target nicht gesehen (nach mindestens 1 Scan)
//   +1  WiFi-Home-SSID nicht in Sicht (nach mindestens 1 Scan)
// Schwellen: evidenceWarnScore → CHECK, evidenceLostScore → LOST
// =============================================================
static uint8_t computeEvidenceScore() {
    uint8_t score = 0;
    if (!gwInRange && lastGwSeenMs > 0 &&
        (millis() - lastGwSeenMs) > (uint32_t)cfg.gwTimeout_s * 1000UL)
        score += 2;
    if (cfg.bleGeofenceEnabled && bleInitDone && bleGeoLastSeenMs > 0 && !bleGeoInZone)
        score += 1;
    if (cfg.wifiEnabled && wifiScanDone && !wifiInZone)
        score += 1;
    return score;
}

// Effektives Beacon-Interval je nach State
static uint32_t getEffectiveBeaconInterval() {
    if (currentState == TrackerState::LOST ||
        currentState == TrackerState::PRECISION)
        return (uint32_t)cfg.beaconIntervalLost_s * 1000UL;
    return (uint32_t)cfg.beaconInterval_s * 1000UL;
}

// Batterie-Stufe: 0=normal 1=reduziert 2=minimal
static uint8_t getBatteryLevel() {
    uint16_t mv = readBatMv();
    if (mv < 3450) return 2;  // < ~10%: Notfall
    if (mv < 3650) return 1;  // < ~30%: reduziert
    return 0;
}

// =============================================================
// CMD-PAKET AUSFÜHREN + RESP senden
// =============================================================
void executeCmdAndRespond(uint8_t cmdId, uint16_t seq,
                           const uint8_t* params, uint8_t paramLen) {
    char respStr[64] = {0};
    uint8_t rc = RC_OK;

    switch (cmdId) {
        case CMD_PING:
            snprintf(respStr, sizeof(respStr), "PONG:%s", cfg.deviceId);
            break;

        case CMD_SET_BEACON_INTERVAL:
            if (paramLen >= 2) {
                cfg.beaconInterval_s = ((uint16_t)params[0] << 8) | params[1];
                snprintf(respStr, sizeof(respStr), "OK:bcn=%d", cfg.beaconInterval_s);
                configDirty = true;
            } else { rc = RC_ERR_PARAM_LEN; snprintf(respStr, sizeof(respStr), "ERR:param_len"); }
            break;

        case CMD_SET_ACK_WINDOW:
            if (paramLen >= 2) {
                cfg.ackWindow_ms = ((uint16_t)params[0] << 8) | params[1];
                snprintf(respStr, sizeof(respStr), "OK:ack=%d", cfg.ackWindow_ms);
                configDirty = true;
            } else { rc = RC_ERR_PARAM_LEN; snprintf(respStr, sizeof(respStr), "ERR:param_len"); }
            break;

        case CMD_SET_PROFILE:
            if (paramLen >= 1) {
                if (params[0] == PROFILE_PRODUCTION) applyProductionProfile(cfg);
                else                                 applyTestProfile(cfg);
                snprintf(respStr, sizeof(respStr), "OK:profile=%s",
                         cfg.profile == PowerProfile::TEST ? "TEST" : "PROD");
                configDirty = true;
            } else { rc = RC_ERR_PARAM_LEN; snprintf(respStr, sizeof(respStr), "ERR:param_len"); }
            break;

        case CMD_SET_STATE:
            if (paramLen >= 1 && params[0] >= 1 && params[0] <= 6) {
                currentState = (TrackerState)params[0];
                snprintf(respStr, sizeof(respStr), "OK:state=%s", stateName(currentState));
            } else { rc = RC_ERR_PARAM_RANGE; snprintf(respStr, sizeof(respStr), "ERR:state_range"); }
            break;

        case CMD_TRIGGER_LOST:
            currentState = TrackerState::LOST;
            snprintf(respStr, sizeof(respStr), "OK:LOST");
            break;

        case CMD_TRIGGER_GPS:
        case CMD_GPS_TRIGGER:   // Alias: MQTT GPS_TRIGGER → auch Tracker startet GPS
            currentState = TrackerState::PRECISION;
            snprintf(respStr, sizeof(respStr), "OK:GPS_START");
            break;

        case CMD_TRIGGER_RECOVERY:
            currentState = TrackerState::RECOVERY;
            snprintf(respStr, sizeof(respStr), "OK:RECOVERY");
            break;

        case CMD_TRIGGER_NORMAL:
            currentState = TrackerState::NORMAL;
            checkFailCount = 0; warningCycles = 0;
            snprintf(respStr, sizeof(respStr), "OK:NORMAL");
            break;

        case CMD_TRIGGER_CHECK:
            lastCheckMs = 0;  // erzwingt sofortigen CHECK beim nächsten Loop
            snprintf(respStr, sizeof(respStr), "OK:CHECK_PENDING");
            break;

        case CMD_SET_868_SF:
            if (paramLen >= 1 && params[0] >= 7 && params[0] <= 12) {
                cfg.sf868 = params[0];
                snprintf(respStr, sizeof(respStr), "OK:sf868=%d", cfg.sf868);
                configDirty = true;
            } else { rc = RC_ERR_PARAM_RANGE; snprintf(respStr, sizeof(respStr), "ERR:sf_range"); }
            break;

        case CMD_SET_868_TX_POWER:
            if (paramLen >= 1) {
                cfg.txPower868 = (int8_t)params[0];
                snprintf(respStr, sizeof(respStr), "OK:pwr868=%d", cfg.txPower868);
                configDirty = true;
            } else { rc = RC_ERR_PARAM_LEN; snprintf(respStr, sizeof(respStr), "ERR:param_len"); }
            break;

        case CMD_SET_433_SF:
            if (paramLen >= 1 && params[0] >= 7 && params[0] <= 12) {
                cfg.sf433 = params[0];
                serial433.printf("SF:%d\n", cfg.sf433);
                snprintf(respStr, sizeof(respStr), "OK:sf433=%d", cfg.sf433);
                configDirty = true;
            } else { rc = RC_ERR_PARAM_RANGE; snprintf(respStr, sizeof(respStr), "ERR:sf_range"); }
            break;

        case CMD_CLEAR_BLE_GEOFENCE:
            cfg.bleGeofenceCount   = 0;
            cfg.bleGeofenceEnabled = false;
            memset(cfg.bleGeofenceMacs,  0, sizeof(cfg.bleGeofenceMacs));
            memset(cfg.bleGeofenceNames, 0, sizeof(cfg.bleGeofenceNames));
            // Geofence-Zustand zurücksetzen
            bleGeoLastSeenMs = 0;
            bleGeoInZone     = false;
            bleGeoAlertFired = false;
            snprintf(respStr, sizeof(respStr), "OK:GEO_CLR");
            configDirty = true;
            break;

        case CMD_SET_BLE_GEOFENCE:
            // Format: [mode 1B][slot 1B][name_len 1B][name bytes...]
            // Beispiel: mode=0, slot=0, name="YANIS-PC" → [0x00][0x00][0x08][Y][A][N][I][S][-][P][C]
            if (paramLen >= 3) {
                uint8_t mode    = params[0];
                uint8_t slot    = params[1] < 3 ? params[1] : 0;
                uint8_t nameLen = params[2];
                if (nameLen > 0 && nameLen <= 31 && (3u + nameLen) <= paramLen) {
                    cfg.bleGeofenceMode = mode;
                    memset(cfg.bleGeofenceNames[slot], 0, 32);
                    memcpy(cfg.bleGeofenceNames[slot], params + 3, nameLen);
                    cfg.bleGeofenceNames[slot][nameLen] = '\0';
                    if (slot >= cfg.bleGeofenceCount) cfg.bleGeofenceCount = slot + 1;
                    cfg.bleGeofenceEnabled = true;
                    // Geo-Zustand neu starten
                    bleGeoLastSeenMs = 0;
                    bleGeoAlertFired = false;
                    snprintf(respStr, sizeof(respStr), "OK:geo[%d]=%s", slot,
                             cfg.bleGeofenceNames[slot]);
                    configDirty = true;
                } else { rc = RC_ERR_PARAM_RANGE; snprintf(respStr, sizeof(respStr), "ERR:name_len"); }
            } else { rc = RC_ERR_PARAM_LEN; snprintf(respStr, sizeof(respStr), "ERR:param_len"); }
            break;

        case CMD_SET_WIFI_CREDENTIALS:
            // params: SSID\0PASSWORD\0
            if (paramLen > 2) {
                strncpy(cfg.wifiSsid, (const char*)params, sizeof(cfg.wifiSsid)-1);
                size_t sl = strlen((const char*)params) + 1;
                if (sl < paramLen)
                    strncpy(cfg.wifiPass, (const char*)(params + sl), sizeof(cfg.wifiPass)-1);
                snprintf(respStr, sizeof(respStr), "OK:wifi_cred");
                configDirty = true;
            } else { rc = RC_ERR_PARAM_LEN; snprintf(respStr, sizeof(respStr), "ERR:param_len"); }
            break;

        case CMD_SET_GW_TIMEOUT:
            if (paramLen >= 2) {
                cfg.gwTimeout_s = ((uint16_t)params[0] << 8) | params[1];
                snprintf(respStr, sizeof(respStr), "OK:gw_to=%d", cfg.gwTimeout_s);
                configDirty = true;
            } else { rc = RC_ERR_PARAM_LEN; snprintf(respStr, sizeof(respStr), "ERR:param_len"); }
            break;

        case CMD_SET_WARNING_TIMEOUT:
            if (paramLen >= 2) {
                cfg.warningTimeout_s = ((uint16_t)params[0] << 8) | params[1];
                snprintf(respStr, sizeof(respStr), "OK:warn_to=%d", cfg.warningTimeout_s);
                configDirty = true;
            } else { rc = RC_ERR_PARAM_LEN; snprintf(respStr, sizeof(respStr), "ERR:param_len"); }
            break;

        case CMD_GET_STATUS:
        case CMD_GET_STATE: {
            snprintf(respStr, sizeof(respStr),
                     "s=%s p=%s bcn=%lu ack=%lu ble=%s%s e=%d w=%s",
                     stateName(currentState),
                     cfg.profile == PowerProfile::TEST ? "T" : "P",
                     beaconCount, ackCount,
                     cfg.bleEnabled ? (bleGeoInZone ? "IN" : "OUT") : "OFF",
                     bleGeoAlertFired ? "!" : "",
                     computeEvidenceScore(),
                     cfg.wifiEnabled ? (wifiInZone ? "IN" : (wifiScanDone ? "OUT" : "?")) : "OFF");
            break;
        }

        case CMD_GET_BATTERY: {
            uint16_t bat = readBatMv();
            snprintf(respStr, sizeof(respStr), "bat=%dmV", bat);
            break;
        }

        case CMD_GET_RSSI:
            snprintf(respStr, sizeof(respStr), "rssi=%d snr=%.1f", lastGwRssi, lastGwSnr);
            break;

        case CMD_SAVE_CONFIG:
            saveConfig();
            snprintf(respStr, sizeof(respStr), "OK:v%u", cfg.configVersion);
            break;

        case CMD_LOAD_CONFIG:
            loadConfig();
            snprintf(respStr, sizeof(respStr), "OK:loaded_v%u", cfg.configVersion);
            break;

        case CMD_ENABLE_MODULE:
            if (paramLen >= 1) {
                switch(params[0]) {
                    case MOD_BLE:
                        cfg.bleEnabled = true;
                        if (!bleInitDone) initBle();
                        break;
                    case MOD_WIFI:    cfg.wifiEnabled    = true; break;
                    case MOD_GPS:     cfg.gpsEnabled     = true; break;
                    case MOD_LORA868: cfg.lora868Enabled = true; break;
                    case MOD_LORA433: cfg.lora433Enabled = true; break;
                    case MOD_LORAWAN: cfg.lorawanEnabled = true; break;
                }
                snprintf(respStr, sizeof(respStr), "OK:en=%d", params[0]);
                configDirty = true;
            } else { rc = RC_ERR_PARAM_LEN; snprintf(respStr, sizeof(respStr), "ERR:param_len"); }
            break;

        case CMD_DISABLE_MODULE:
            if (paramLen >= 1) {
                switch(params[0]) {
                    case MOD_BLE:
                        cfg.bleEnabled = false;
                        if (bleScanRunning) {
                            NimBLEDevice::getScan()->stop();
                            bleScanRunning = false;
                        }
                        break;
                    case MOD_WIFI:    cfg.wifiEnabled    = false; break;
                    case MOD_GPS:     cfg.gpsEnabled     = false; break;
                    case MOD_LORA433: cfg.lora433Enabled = false; break;
                    case MOD_LORAWAN: cfg.lorawanEnabled = false; break;
                }
                snprintf(respStr, sizeof(respStr), "OK:dis=%d", params[0]);
                configDirty = true;
            } else { rc = RC_ERR_PARAM_LEN; snprintf(respStr, sizeof(respStr), "ERR:param_len"); }
            break;

        case CMD_REBOOT: {
            uint8_t del = (paramLen >= 1) ? params[0] : 3;
            snprintf(respStr, sizeof(respStr), "OK:reboot_%ds", del);
            // RESP senden, dann rebooten
            uint8_t respFrame[32];
            size_t rlen = buildRespFrame(respFrame, sizeof(respFrame), cmdId, seq, rc, respStr);
            if (rlen > 0) radio->transmit(respFrame, rlen);
            delay(del * 1000UL);
            ESP.restart();
            return;
        }

        case CMD_FACTORY_RESET:
            if (paramLen >= 1 && params[0] == 0xAA) {
                cfg = TrackerConfig();
                configDirty = true;
                snprintf(respStr, sizeof(respStr), "OK:factory_reset");
                uint8_t respFrame[32];
                size_t rlen = buildRespFrame(respFrame, sizeof(respFrame), cmdId, seq, rc, respStr);
                if (rlen > 0) radio->transmit(respFrame, rlen);
                delay(2000);
                ESP.restart();
                return;
            } else { rc = RC_ERR_AUTH; snprintf(respStr, sizeof(respStr), "ERR:confirm_0xAA"); }
            break;

        case CMD_SET_WAKE_INTERVAL:
            if (paramLen >= 2) {
                cfg.wakeInterval_s = ((uint16_t)params[0] << 8) | params[1];
                snprintf(respStr, sizeof(respStr), "OK:wake=%d", cfg.wakeInterval_s);
                configDirty = true;
            } else { rc = RC_ERR_PARAM_LEN; snprintf(respStr, sizeof(respStr), "ERR:param_len"); }
            break;

        case CMD_SET_BLE_SCAN_DURATION:
            if (paramLen >= 2) {
                cfg.bleScanDuration_ms = ((uint16_t)params[0] << 8) | params[1];
                snprintf(respStr, sizeof(respStr), "OK:ble_scan=%d", cfg.bleScanDuration_ms);
                configDirty = true;
            } else { rc = RC_ERR_PARAM_LEN; snprintf(respStr, sizeof(respStr), "ERR:param_len"); }
            break;

        case CMD_SET_ACTIVE_TIME:
            if (paramLen >= 4) {
                cfg.activeTime_ms = ((uint32_t)params[0] << 24) | ((uint32_t)params[1] << 16)
                                  | ((uint32_t)params[2] <<  8) |  (uint32_t)params[3];
                snprintf(respStr, sizeof(respStr), "OK:active=%lu", (unsigned long)cfg.activeTime_ms);
                configDirty = true;
            } else { rc = RC_ERR_PARAM_LEN; snprintf(respStr, sizeof(respStr), "ERR:param_len"); }
            break;

        case CMD_SET_DEVICE_ID:
            if (paramLen >= 1 && paramLen <= 15) {
                memcpy(cfg.deviceId, params, paramLen);
                cfg.deviceId[paramLen] = '\0';
                snprintf(respStr, sizeof(respStr), "OK:id=%s", cfg.deviceId);
                configDirty = true;
            } else { rc = RC_ERR_PARAM_RANGE; snprintf(respStr, sizeof(respStr), "ERR:id_len"); }
            break;

        case CMD_GET_CONFIG:
            snprintf(respStr, sizeof(respStr), "v%u p%s bcn%d wake%d sf%d pwr%d",
                     cfg.configVersion,
                     cfg.profile == PowerProfile::TEST ? "T" : "P",
                     cfg.beaconInterval_s, cfg.wakeInterval_s,
                     cfg.sf868, cfg.txPower868);
            break;

        case CMD_GET_BLE_GEOFENCE:
            snprintf(respStr, sizeof(respStr), "cnt=%d en=%d mode=%d t0=%.16s",
                     cfg.bleGeofenceCount,
                     cfg.bleGeofenceEnabled ? 1 : 0,
                     cfg.bleGeofenceMode,
                     cfg.bleGeofenceNames[0]);
            break;

        case CMD_GPS_STOP:
            if (currentState == TrackerState::PRECISION)
                currentState = TrackerState::NORMAL;
            snprintf(respStr, sizeof(respStr), "OK:GPS_STOP");
            break;

        case CMD_GPS_CONTINUOUS:
            currentState = TrackerState::PRECISION;
            snprintf(respStr, sizeof(respStr), "OK:GPS_CONT");
            break;

        case CMD_FORCE_LORAWAN_JOIN:
            joined = false;
            lastUplink = 0;
            joinRetryCount = 0;
            snprintf(respStr, sizeof(respStr), "OK:JOIN_PENDING");
            break;

        case CMD_BLE_SCAN:
            if (cfg.bleEnabled && bleInitDone) {
                bleNextScanMs = 0;  // sofortiger Scan beim nächsten pollBleScan()
                snprintf(respStr, sizeof(respStr), "OK:BLE_SCAN");
            } else {
                rc = RC_ERR_NOT_SUPPORTED;
                snprintf(respStr, sizeof(respStr), "ERR:ble_disabled");
            }
            break;

        case CMD_WIFI_SCAN:
            if (cfg.wifiEnabled) {
                wifiNextScanMs = 0;   // sofortiger Scan im nächsten pollWifiScanGeo()
                snprintf(respStr, sizeof(respStr), "OK:WIFI_SCAN_PEND");
            } else {
                rc = RC_ERR_NOT_SUPPORTED;
                snprintf(respStr, sizeof(respStr), "ERR:wifi_disabled");
            }
            break;

        // Noch nicht implementierte Module → NOT_SUPPORTED
        case CMD_WIFI_CONNECT:
        case CMD_WIFI_DISCONNECT:
            rc = RC_ERR_NOT_SUPPORTED;
            snprintf(respStr, sizeof(respStr), "ERR:not_impl");
            break;

        default:
            rc = RC_ERR_UNKNOWN_CMD;
            snprintf(respStr, sizeof(respStr), "ERR:unk_0x%02X", cmdId);
            break;
    }

    // RESP-Frame senden
    uint8_t respFrame[64];
    size_t rlen = buildRespFrame(respFrame, sizeof(respFrame), cmdId, seq, rc, respStr);
    if (rlen > 0) {
        int16_t txRc = radio->transmit(respFrame, rlen);
        Serial.printf("[CMD] 0x%02X -> %s (rc=%d, tx=%d)\n", cmdId, respStr, rc, txRc);
    }
}

// =============================================================
// GPS-PAKET SENDEN (immer wenn gpsEnabled, Fix optional)
// Format: [PKT_GPS][ID 6B][lat 4B][lon 4B][alt 2B][sats 1B][hdop 1B][0xAB] = 20B
// lat/lon: degrees * 1e6 (int32) | sats=0 + hdop=255 → kein Fix (Searching)
// =============================================================
void sendGpsPacket() {
    uint8_t pkt[20] = {};
    pkt[0] = PKT_GPS;
    const char* tid = cfg.deviceId;
    for (int i = 0; i < 6 && tid[i]; i++) pkt[1 + i] = (uint8_t)tid[i];

    int32_t lat_i  = 0;
    int32_t lon_i  = 0;
    int16_t alt_i  = 0;
    uint8_t sats   = 0;
    uint8_t hdop_i = 255;  // 255 = kein Fix

    if (hasGpsFix()) {
        lat_i  = (int32_t)(lastGpsFix.lat * 1e6);
        lon_i  = (int32_t)(lastGpsFix.lon * 1e6);
        alt_i  = (int16_t)constrain((int)lastGpsFix.altM, -32768, 32767);
        sats   = lastGpsFix.sats;
        hdop_i = (uint8_t)constrain((int)(lastGpsFix.hdop * 10.0f), 0, 254);
    }

    pkt[7]  = (uint8_t)((lat_i >> 24) & 0xFF);
    pkt[8]  = (uint8_t)((lat_i >> 16) & 0xFF);
    pkt[9]  = (uint8_t)((lat_i >>  8) & 0xFF);
    pkt[10] = (uint8_t)( lat_i        & 0xFF);
    pkt[11] = (uint8_t)((lon_i >> 24) & 0xFF);
    pkt[12] = (uint8_t)((lon_i >> 16) & 0xFF);
    pkt[13] = (uint8_t)((lon_i >>  8) & 0xFF);
    pkt[14] = (uint8_t)( lon_i        & 0xFF);
    pkt[15] = (uint8_t)((alt_i >>  8) & 0xFF);
    pkt[16] = (uint8_t)( alt_i        & 0xFF);
    pkt[17] = sats;
    pkt[18] = hdop_i;
    pkt[19] = 0xAB;

    int16_t rc = radio->transmit(pkt, sizeof(pkt));
    if (rc == RADIOLIB_ERR_NONE) {
        if (hasGpsFix()) {
            Serial.printf("[GPS] TX Fix: %.6f,%.6f alt=%.1fm sats=%d hdop=%.1f\n",
                          lastGpsFix.lat, lastGpsFix.lon, lastGpsFix.altM, sats,
                          lastGpsFix.hdop);
        } else {
            Serial.printf("[GPS] TX Searching: chars=%lu sats_vis=%u\n",
                          (unsigned long)gps.charsProcessed(),
                          gps.satellites.isValid() ? gps.satellites.value() : 0);
        }
    }
}

// =============================================================
// DEEP SLEEP
// Sichert Zustand in RTC-Memory, schaltet Peripherie ab,
// schläft cfg.sleepTime_s Sekunden. Setup() wird beim Aufwachen
// erneut aufgerufen – RTC-Variablen bleiben erhalten.
// =============================================================
void goToDeepSleep() {
    // Zustand in RTC sichern
    rtcBeaconCount = beaconCount;
    rtcAckCount    = ackCount;
    rtcLogSeq      = logSeq;
    rtcUplinkCount = uplinkCount;
    rtcState       = (uint8_t)currentState;
    rtcJoined      = joined;
    rtcGwInRange   = gwInRange;
    rtcLastGwRssi  = lastGwRssi;
    rtcBootCount++;

    // Radio schlafen legen
    radio->sleep();

    // GPS UART schließen
    if (cfg.gpsEnabled) serialGps.end();

    // 433 UART schließen
    serial433.end();

    // Display ausschalten
    display.ssd1306_command(SSD1306_DISPLAYOFF);

    // VEXT abschalten (HIGH = aus auf Heltec V3)
    digitalWrite(VEXT_CTRL, HIGH);

    // LED aus
    digitalWrite(STATUS_LED, LOW);

    uint64_t sleepUs = (uint64_t)cfg.sleepTime_s * 1000000ULL;
    esp_sleep_enable_timer_wakeup(sleepUs);

    Serial.printf("[SLEEP] Deep Sleep %us | Boot#%lu | State=%s\n",
                  cfg.sleepTime_s, (unsigned long)rtcBootCount,
                  stateName(currentState));
    Serial.flush();
    delay(50);

    esp_deep_sleep_start();
    // kommt nicht zurück
}

// =============================================================
// RADIO IN BEACON-MODUS SETZEN
// =============================================================
void radioSetBeaconMode()
{
    radio->setFrequency(cfg.freq868_mhz);
    radio->setBandwidth(cfg.bw868_khz);
    radio->setSpreadingFactor(cfg.sf868);
    radio->setCodingRate(cfg.cr868);
    radio->setSyncWord(cfg.sync868);
    radio->setOutputPower(cfg.txPower868);
    radio->setCurrentLimit(140.0f);
}

// =============================================================
// BEACON SENDEN + ACK EMPFANGEN (bidirektional)
//
// Ablauf:
//   1. Beacon TX (11 Bytes)
//   2. Sofort RX aktivieren – 600ms auf ACK warten (sync=0xAB)
//   3. ACK parsen (RSSI, SNR vom Gateway)
//   4. Wenn ACK erhalten: LOG-Paket senden
//   5. Standby – Aufrufer setzt Sync Word zurück
// =============================================================
void sendBeaconAndListen()
{
    beaconCount++;

    // --- Beacon TX ---
    uint8_t pkt[11] = {0};
    const char* tid = cfg.deviceId;
    for (int i = 0; i < 6 && tid[i]; i++) pkt[i] = (uint8_t)tid[i];
    pkt[6] = (beaconCount >> 8) & 0xFF;
    pkt[7] =  beaconCount       & 0xFF;
    uint16_t batMv = readBatMv();
    pkt[8] = (batMv >> 8) & 0xFF;
    pkt[9] =  batMv       & 0xFF;
    pkt[10] = 0xAB;

    int16_t rc = radio->transmit(pkt, sizeof(pkt));
    if (rc == RADIOLIB_ERR_NONE) {
        Serial.printf("[BCN] #%lu OK bat=%dmV\n", beaconCount, batMv);
        if (cfg.lora433Enabled) relay433(pkt, sizeof(pkt));
    } else {
        Serial.printf("[BCN] Fehler: %d\n", rc);
    }

    // --- RX-Fenster: warte auf ACK vom Gateway ---
    // radio->available() ist auf SX126x ohne angehängten IRQ-Handler immer 0.
    // Wir nutzen setPacketReceivedAction() + volatile Flag stattdessen.
    ackPacketReady = false;
    radio->setPacketReceivedAction(onAckPacket);
    radio->startReceive();
    uint32_t rxStart = millis();
    bool ackReceived = false;

    while (millis() - rxStart < cfg.ackWindow_ms) {
        if (ackPacketReady) {
            ackPacketReady = false;
            uint8_t ackBuf[16] = {0};
            size_t  ackLen = radio->getPacketLength();
            if (ackLen > sizeof(ackBuf)) ackLen = sizeof(ackBuf);
            if (radio->readData(ackBuf, ackLen) == RADIOLIB_ERR_NONE) {
                parseAck(ackBuf, ackLen);
                ackReceived = true;
            }
            break;
        }
        delay(1);
    }
    radio->clearPacketReceivedAction();

    // --- CMD-Fenster: nach ACK kurz auf Gateway-Command warten ---
    // Gateway sendet CMD unmittelbar nach ACK wenn ein Befehl aussteht
    {
        ackPacketReady = false;
        radio->setPacketReceivedAction(onAckPacket);
        radio->startReceive();
        uint32_t cmdStart = millis();
        while (millis() - cmdStart < cfg.cmdWindow_ms) {
            if (ackPacketReady) {
                ackPacketReady = false;
                uint8_t cmdBuf[64] = {0};
                size_t  cmdLen = radio->getPacketLength();
                if (cmdLen > sizeof(cmdBuf)) cmdLen = sizeof(cmdBuf);
                if (radio->readData(cmdBuf, cmdLen) == RADIOLIB_ERR_NONE
                    && cmdBuf[0] == PKT_CMD) {
                    uint8_t  cmdId, cFlags, cParamLen;
                    uint16_t cSeq;
                    const uint8_t* cParams;
                    if (parseCmdFrame(cmdBuf, cmdLen, cmdId, cSeq, cFlags, &cParams, cParamLen)) {
                        Serial.printf("[CMD] Empfangen: 0x%02X seq=%d\n", cmdId, cSeq);
                        executeCmdAndRespond(cmdId, cSeq, cParams, cParamLen);
                    } else {
                        Serial.println("[CMD] CRC/Frame Fehler");
                    }
                }
                break;
            }
            delay(1);
        }
        radio->clearPacketReceivedAction();
    }

    radio->standby();

    // --- State Machine: GW-Timeout prüfen ---
    uint32_t gwAge = millis() - lastAckTime;
    if (ackReceived) {
        lastGwSeenMs   = millis();
        checkFailCount = 0;
        warningCycles  = 0;
        if (currentState == TrackerState::WARNING  ||
            currentState == TrackerState::LOST     ||
            currentState == TrackerState::RECOVERY)
            currentState = TrackerState::NORMAL;
    } else if (gwInRange && (gwAge > (uint32_t)cfg.gwTimeout_s * 1000UL)) {
        gwInRange = false;
        Serial.println("[GW] Timeout");
    }

    // --- LOG-Paket senden wenn ACK erhalten ---
    if (ackReceived && gwInRange) {
        delay(10);
        sendLogPacket();
        // GPS-Paket: immer wenn GPS aktiv (auch ohne Fix → "Searching" Status)
        if (cfg.gpsEnabled) {
            delay(10);
            sendGpsPacket();
        }
    }
}

// =============================================================
// SETUP
// =============================================================
void setup()
{
    Serial.begin(115200);
    {
        uint32_t t0 = millis();
        while (!Serial && (millis() - t0) < 5000) { delay(10); }
        delay(100);
    }
    Serial.println("[BOOT] " FIRMWARE_VERSION);
    Serial.println("[BOOT] SmartTag Tag | Bidir 868MHz + 433MHz Modem | EU868");

    // Config laden (NVS) – vor allem anderen
    loadConfig();

    // Deep-Sleep Wakeup erkennen und RTC-Zustand wiederherstellen
    esp_sleep_wakeup_cause_t wakeupCause = esp_sleep_get_wakeup_cause();
    bool isWakeFromSleep = (wakeupCause == ESP_SLEEP_WAKEUP_TIMER);
    if (isWakeFromSleep && rtcBootCount > 0) {
        beaconCount  = rtcBeaconCount;
        ackCount     = rtcAckCount;
        logSeq       = rtcLogSeq;
        uplinkCount  = rtcUplinkCount;
        joined       = rtcJoined;
        gwInRange    = rtcGwInRange;
        lastGwRssi   = rtcLastGwRssi;
        currentState = (TrackerState)rtcState;
        Serial.printf("[BOOT] Wakeup #%lu | State=%s | joined=%d\n",
                      (unsigned long)rtcBootCount, stateName(currentState), joined);
    } else {
        Serial.println("[BOOT] Erster Start (kein Deep Sleep)");
    }

    // 433MHz Modem (CubeCell)
    serial433.begin(MODEM433_BAUD, SERIAL_8N1, MODEM433_RX_PIN, MODEM433_TX_PIN);
    Serial.printf("[433] UART2: RX=GPIO%d TX=GPIO%d\n",
                  MODEM433_RX_PIN, MODEM433_TX_PIN);

    // GPS (NEO-M9N, UART1)
    if (cfg.gpsEnabled) {
        serialGps.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
        Serial.printf("[GPS] UART1: RX=GPIO%d TX=GPIO%d @%lubaud – teste...\n",
                      GPS_RX_PIN, GPS_TX_PIN, (unsigned long)GPS_BAUD);
        Serial.flush();

        // 2s lauschen: Bytes zählen + erste NMEA-Sätze loggen
        uint32_t gpsTestEnd  = millis() + 2000;
        uint32_t bytesTotal  = 0;
        uint8_t  sentencesCnt = 0;
        char     nmeaBuf[90];
        uint8_t  nmeaPos = 0;
        bool     inSentence = false;

        while (millis() < gpsTestEnd) {
            while (serialGps.available()) {
                char c = (char)serialGps.read();
                bytesTotal++;
                gps.encode(c);   // TinyGPS++ mitfüttern

                // Rohe NMEA-Sätze mitlesen ($....\n)
                if (c == '$') { inSentence = true; nmeaPos = 0; }
                if (inSentence && nmeaPos < sizeof(nmeaBuf) - 1) {
                    nmeaBuf[nmeaPos++] = c;
                    if (c == '\n') {
                        nmeaBuf[nmeaPos] = '\0';
                        if (sentencesCnt < 4) {   // max 4 ausgeben
                            // Zeilenende entfernen für saubere Ausgabe
                            for (int k = nmeaPos - 1; k >= 0; k--) {
                                if (nmeaBuf[k] == '\r' || nmeaBuf[k] == '\n')
                                    nmeaBuf[k] = '\0';
                                else break;
                            }
                            Serial.printf("[GPS] NMEA: %s\n", nmeaBuf);
                            Serial.flush();
                        }
                        sentencesCnt++;
                        inSentence = false;
                        nmeaPos = 0;
                    }
                }
            }
        }

        if (bytesTotal == 0) {
            Serial.println("[GPS] WARNUNG: 0 Bytes empfangen – Verkabelung prüfen!");
            Serial.printf("      GPIO%d (RX) muss mit NEO-M9N TX verbunden sein.\n", GPS_RX_PIN);
            Serial.printf("      3.3V und GND prüfen.\n");
        } else if (sentencesCnt == 0) {
            Serial.printf("[GPS] %lu Bytes, aber kein NMEA '$' – falsches Baud? (aktuell %lu)\n",
                          bytesTotal, (unsigned long)GPS_BAUD);
        } else {
            Serial.printf("[GPS] OK – %lu Bytes, %u NMEA-Saetze in 2s erkannt\n",
                          bytesTotal, sentencesCnt);
        }
    } else {
        Serial.println("[GPS] deaktiviert (cfg.gpsEnabled=false)");
        Serial.println("[GPS] Aktivieren: CMD ENABLE_MODULE module=3, dann SAVE_CONFIG");
    }
    // BLE initialisieren (wenn aktiviert)
    if (cfg.bleEnabled) {
        initBle();
    } else {
        Serial.println("[BLE] deaktiviert (cfg.bleEnabled=false)");
    }

    // WiFi im Scan-Only Modus vorbereiten (kein Connect)
    if (cfg.wifiEnabled) {
        WiFi.mode(WIFI_STA);
        WiFi.disconnect(true);   // kein Auto-Connect zu alten APs
        wifiNextScanMs = millis() + 15000UL;  // erster Scan nach 15s
        Serial.printf("[WiFi-GEO] Scan-Mode aktiv – Home-SSID: '%s'\n",
                      cfg.wifiGeofenceEnabled && cfg.wifiGeofenceSsid[0]
                          ? cfg.wifiGeofenceSsid : cfg.wifiSsid);
    } else {
        WiFi.mode(WIFI_OFF);
        Serial.println("[WiFi] deaktiviert (cfg.wifiEnabled=false)");
    }

    Serial.printf("[BOOT] DevEUI: %016llX\n", (unsigned long long)LORAWAN_DEVEUI);
    Serial.flush();

    pinMode(STATUS_LED, OUTPUT);
    digitalWrite(STATUS_LED, LOW);

    pinMode(VEXT_CTRL, OUTPUT);
    digitalWrite(VEXT_CTRL, LOW);
    delay(50);

    pinMode(OLED_RST, OUTPUT);
    digitalWrite(OLED_RST, LOW);
    delay(20);
    digitalWrite(OLED_RST, HIGH);
    delay(20);

    Wire.begin(OLED_SDA, OLED_SCL);
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
        Serial.println("[ERROR] OLED Init fehlgeschlagen");
        while (true) { digitalWrite(STATUS_LED, !digitalRead(STATUS_LED)); delay(200); }
    }
    drawStatus("Booting...", FIRMWARE_VERSION, "TTN+Beacon+Bidir");
    delay(400);

    SPI.begin(9, 11, 10, LORA_NSS);

    radio = new SX1262(new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY));

    Serial.println("[LORA] SX1262 init...");
    int16_t rc = radio->begin();
    if (rc != RADIOLIB_ERR_NONE && rc != RADIOLIB_ERR_CHIP_NOT_FOUND) {
        Serial.printf("[ERROR] SX1262 begin: %d\n", rc);
        drawStatus("SX1262 Fehler!", ("Code: " + String(rc)).c_str(), "SPI Pins prüfen");
        while (true) { digitalWrite(STATUS_LED, !digitalRead(STATUS_LED)); delay(500); }
    }
    Serial.println("[LORA] SX1262 OK");
    radio->setRxBoostedGainMode(true);
    Serial.println("[LORA] RX Boosted Gain aktiv");

    node = new LoRaWANNode(radio, &EU868);

    uint8_t appkey[] = LORAWAN_APPKEY;
    node->beginOTAA(LORAWAN_APPEUI, LORAWAN_DEVEUI, appkey, appkey);

    prefs.begin("lorawan", false);
    {
        uint8_t nonceBuf[RADIOLIB_LORAWAN_NONCES_BUF_SIZE] = {0};
        uint8_t sessBuf [RADIOLIB_LORAWAN_SESSION_BUF_SIZE] = {0};
        size_t  nLen = prefs.getBytes("nonces",  nonceBuf, sizeof(nonceBuf));
        size_t  sLen = prefs.getBytes("session", sessBuf,  sizeof(sessBuf));

        if (nLen == sizeof(nonceBuf) && sLen == sizeof(sessBuf)) {
            node->setBufferNonces(nonceBuf);
            node->setBufferSession(sessBuf);
            Serial.println("[LORA] NVS geladen – versuche Session-Restore...");
        } else {
            Serial.println("[LORA] Keine NVS-Daten – frischer OTAA Join");
        }
    }

    drawStatus("OTAA Join...", "EU868 TTN", "Suche Gateway...");
    for (int attempt = 1; attempt <= 2; attempt++) {
        Serial.printf("[LORA] Join Versuch %d/2...\n", attempt);
        drawStatus("Joining...", ("Versuch " + String(attempt) + "/2").c_str(), "EU868");

        for (int i = 0; i < 5; i++) {
            digitalWrite(STATUS_LED, !digitalRead(STATUS_LED));
            delay(200);
        }

        rc = node->activateOTAA(0);
        if (rc == RADIOLIB_LORAWAN_NEW_SESSION ||
            rc == RADIOLIB_LORAWAN_SESSION_RESTORED ||
            rc == RADIOLIB_ERR_NONE) {
            joined = true;
            prefs.putBytes("nonces",  node->getBufferNonces(),  RADIOLIB_LORAWAN_NONCES_BUF_SIZE);
            prefs.putBytes("session", node->getBufferSession(), RADIOLIB_LORAWAN_SESSION_BUF_SIZE);
            Serial.printf("[LORA] JOIN OK (rc=%d)!\n", rc);
            drawStatus("JOINED!", "TTN OK", ("TX in " + String(UPLINK_INTERVAL_MS/1000) + "s").c_str());
            digitalWrite(STATUS_LED, HIGH); delay(500); digitalWrite(STATUS_LED, LOW);
            break;
        }
        Serial.printf("[LORA] Join Fehler: %d – warte 45s\n", rc);
        prefs.putBytes("nonces", node->getBufferNonces(), RADIOLIB_LORAWAN_NONCES_BUF_SIZE);
        if (attempt < 2) {
            drawStatus("Join Fehler", ("Err:" + String(rc)).c_str(), "Retry in 45s...");
            delay(45000);
        }
    }

    if (!joined) {
        Serial.println("[LORA] Kein Join. Beacon + Bidir aktiv.");
        drawStatus("Join failed", "Beacon+Bidir", "NEAR_FIND OK");
    }

    // Erster Beacon + ACK-Listen sofort
    radioSetBeaconMode();
    sendBeaconAndListen();
    radio->setSyncWord(0x34, 0x44);  // zurück zu LoRaWAN
    lastBeacon = millis();
}

// =============================================================
// LOOP
// =============================================================
void loop()
{
    uint32_t now = millis();

    // --- GPS pollen (wenn aktiv) ---
    if (cfg.gpsEnabled) {
        pollGps();
        // PRECISION-State: GPS Fix abwarten → dann RECOVERY
        if (currentState == TrackerState::PRECISION && hasGpsFix()) {
            Serial.printf("[GPS] Fix! %.6f,%.6f alt=%.1fm sats=%d hdop=%.1f\n",
                          lastGpsFix.lat, lastGpsFix.lon,
                          lastGpsFix.altM, lastGpsFix.sats, lastGpsFix.hdop);
            currentState = TrackerState::RECOVERY;
        }
    }

    // --- 433MHz Modem pollen ---
    poll433();

    // --- BLE Geofence pollen ---
    pollBleScan();

    // --- WiFi Geofence-Scan pollen ---
    pollWifiScanGeo();

    // --- Ping Modem wenn noch nicht bereit (alle 5s) ---
    if (!m433Ready && (now - last433PingMs > 5000)) {
        last433PingMs = now;
        serial433.println("STATUS");
    }

    // LED blinken
    uint32_t blinkMs = joined ? 1500 : 400;
    if (now - lastBlink > blinkMs) {
        lastBlink = now;
        ledState = !ledState;
        digitalWrite(STATUS_LED, ledState);
    }

    // --- Evidence-Check-Zyklus (alle wakeInterval_s) ---
    // Bewertet GW + BLE + WiFi kombiniert → Evidence Score → State
    {
        static uint32_t lastEvidenceMs = 0;
        if ((now - lastEvidenceMs) >= (uint32_t)cfg.wakeInterval_s * 1000UL) {
            lastEvidenceMs = now;

            // WiFi-Scan starten wenn nicht schon laufend
            if (cfg.wifiEnabled && !wifiScanRunning_g && !bleScanRunning) {
                wifiNextScanMs = 0;
            }

            uint8_t score = computeEvidenceScore();
            Serial.printf("[EVD] Score=%d/4 gw=%s ble=%s wifi=%s state=%s\n",
                          score,
                          gwInRange ? "OK" : "MISS",
                          (cfg.bleGeofenceEnabled && bleInitDone) ?
                              (bleGeoInZone ? "OK" : "MISS") : "-",
                          cfg.wifiEnabled ?
                              (wifiInZone ? "OK" : (wifiScanDone ? "MISS" : "?")) : "-",
                          stateName(currentState));

            // Sofort-Eskalation bei maximaler Evidence (alle Quellen fehlen)
            if ((currentState == TrackerState::NORMAL  ||
                 currentState == TrackerState::CHECK   ||
                 currentState == TrackerState::WARNING) &&
                score >= cfg.evidenceLostScore) {
                currentState  = TrackerState::LOST;
                warningCycles = cfg.warningFailsForLost;
                Serial.printf("[SM/EVD] → LOST (Score=%d)\n", score);
            }
            // Warnstufe: mehrere Quellen fehlen
            else if (currentState == TrackerState::NORMAL &&
                     score >= cfg.evidenceWarnScore) {
                currentState = TrackerState::CHECK;
                Serial.printf("[SM/EVD] → CHECK (Score=%d)\n", score);
            }
            // Entwarnung: Score wieder unter Schwelle → RECOVERY falls LOST/WARNING
            else if ((currentState == TrackerState::WARNING ||
                      currentState == TrackerState::CHECK)  &&
                     score == 0) {
                currentState  = TrackerState::NORMAL;
                warningCycles = 0;
                checkFailCount = 0;
                Serial.println("[SM/EVD] → NORMAL (Score=0, Entwarnung)");
            }
        }
    }

    // --- State Machine Übergänge (GW-Timeout-Fallback) ---
    // Backup-Pfad wenn NUR der GW-Timeout überschritten ist (kein BLE/WiFi aktiv)
    {
        static uint32_t lastSmCheckMs = 0;
        bool smDue = (now - lastSmCheckMs) >= (uint32_t)cfg.beaconInterval_s * 1000UL;
        if (smDue && lastGwSeenMs > 0 && !gwInRange
            && (now - lastGwSeenMs) > (uint32_t)cfg.warningTimeout_s * 1000UL) {
            if (currentState == TrackerState::NORMAL ||
                currentState == TrackerState::CHECK  ||
                currentState == TrackerState::WARNING) {
                lastSmCheckMs = now;
                warningCycles++;
                if (warningCycles >= cfg.warningFailsForLost) {
                    currentState = TrackerState::LOST;
                    Serial.println("[SM] → LOST (GW-Timeout)");
                } else {
                    currentState = TrackerState::WARNING;
                    Serial.printf("[SM] → WARNING (cycle %d/%d)\n",
                                  warningCycles, cfg.warningFailsForLost);
                }
            }
        }
    }

    // --- Batterie-abhängiges Verhalten ---
    {
        static uint32_t lastBatCheckMs = 0;
        if ((now - lastBatCheckMs) > 30000) {
            lastBatCheckMs = now;
            uint8_t batLvl = getBatteryLevel();
            if (batLvl == 2 && cfg.wifiEnabled) {
                // Unter 10%: WiFi-Scan deaktivieren um Energie zu sparen
                wifiNextScanMs = now + 600000UL;  // 10 min pause
                Serial.println("[BAT] Kritisch (<10%): WiFi-Scan pausiert");
            }
        }
    }
    // LOST → PRECISION (wird in executeCmdAndRespond oder GPS-Fix gesetzt)
    // PRECISION → RECOVERY (wird in executeCmdAndRespond gesetzt)
    // RECOVERY → NORMAL (nach 60s mit GW in Sicht)
    // RECOVERY → LOST  (nach 5min ohne GW, z.B. nach NEAR_FIND ohne Heimweg)
    {
        static uint32_t recoveryStart = 0;
        if (currentState == TrackerState::RECOVERY) {
            if (recoveryStart == 0) recoveryStart = now;
            if (gwInRange && (now - recoveryStart > 60000)) {
                currentState  = TrackerState::NORMAL;
                recoveryStart = 0;
                warningCycles = 0;
                checkFailCount = 0;
                Serial.println("[SM] RECOVERY → NORMAL (GW in Sicht, 60s)");
            } else if (!gwInRange && (now - recoveryStart > 300000)) {
                // Kein GW nach 5min (z.B. NEAR_FIND im Feld) → zurück zu LOST
                currentState  = TrackerState::LOST;
                recoveryStart = 0;
                Serial.println("[SM] RECOVERY → LOST (5min kein GW)");
            }
        } else {
            recoveryStart = 0;  // Reset wenn State nicht RECOVERY
        }
    }

    // --- BEACON + ACK-Fenster (state-adaptive Interval) ---
    if (now - lastBeacon >= getEffectiveBeaconInterval()) {
        lastBeacon = now;
        radioSetBeaconMode();
        sendBeaconAndListen();
        // Sync Word zurück auf LoRaWAN Public (SX1262: 0x3444)
        radio->setSyncWord(0x34, 0x44);

        // Deep Sleep wenn aktiviert (nach Beacon-Zyklus)
        if (cfg.deepSleepEnabled) {
            // LoRaWAN Uplink vor Sleep (wenn joined und fällig)
            // Jeder N-te Wakeup = Uplink (sleepTime_s * N ≈ UPLINK_INTERVAL)
            if (joined) {
                uint32_t bootsPerUplink = (uint32_t)(UPLINK_INTERVAL_MS / ((uint32_t)cfg.sleepTime_s * 1000UL));
                if (bootsPerUplink < 1) bootsPerUplink = 1;
                if ((rtcBootCount % bootsPerUplink) == 0) {
                    // LoRaWAN Uplink (gleiche Logik wie im normalen Loop)
                    uplinkCount++;
                    uint8_t payload[11] = {0};
                    const char* tid = cfg.deviceId;
                    for (int i = 0; i < 6 && tid[i]; i++) payload[i] = (uint8_t)tid[i];
                    payload[6] = (uplinkCount >> 8) & 0xFF;
                    payload[7] =  uplinkCount       & 0xFF;
                    uint16_t bMv = readBatMv();
                    payload[8] = (bMv >> 8) & 0xFF;
                    payload[9] =  bMv       & 0xFF;
                    payload[10] = (uint8_t)currentState;
                    int16_t lrc = node->sendReceive(payload, sizeof(payload), 1);
                    if (lrc >= 0) {
                        prefs.putBytes("session", node->getBufferSession(),
                                       RADIOLIB_LORAWAN_SESSION_BUF_SIZE);
                        Serial.printf("[TTN] Uplink #%lu OK\n", uplinkCount);
                    } else {
                        Serial.printf("[TTN] Uplink Fehler: %d\n", lrc);
                    }
                }
            }
            goToDeepSleep();   // kehrt nicht zurück
        }
    }

    // --- LORAWAN UPLINK (alle 30s) ---
    if (joined && (now - lastUplink >= UPLINK_INTERVAL_MS)) {
        lastUplink = now;
        uplinkCount++;

        uint8_t payload[11] = {0};
        const char* tid = cfg.deviceId;
        for (int i = 0; i < 6 && tid[i]; i++) payload[i] = (uint8_t)tid[i];
        payload[6] = (uplinkCount >> 8) & 0xFF;
        payload[7] =  uplinkCount       & 0xFF;
        uint16_t batMv = readBatMv();
        payload[8] = (batMv >> 8) & 0xFF;
        payload[9] =  batMv       & 0xFF;
        payload[10] = (uint8_t)currentState;

        Serial.printf("[TX] #%lu sende %d Bytes...\n", uplinkCount, (int)sizeof(payload));
        drawStatus("Sende TTN...", ("#" + String(uplinkCount)).c_str(), lastStatus.c_str());

        int16_t state = node->sendReceive(payload, sizeof(payload), 1);
        if (state == RADIOLIB_ERR_NONE) {
            lastStatus = "TX OK";
            Serial.println("[TX] OK");
            prefs.putBytes("session", node->getBufferSession(), RADIOLIB_LORAWAN_SESSION_BUF_SIZE);
        } else if (state > 0) {
            lastStatus = "DL " + String(state) + "B";
            Serial.printf("[TX] OK + Downlink %d Bytes\n", state);
            prefs.putBytes("session", node->getBufferSession(), RADIOLIB_LORAWAN_SESSION_BUF_SIZE);
        } else {
            lastStatus = "Err " + String(state);
            Serial.printf("[TX] Fehler: %d\n", state);
        }
    } else if (joined) {
        uint32_t rem = (UPLINK_INTERVAL_MS - (now - lastUplink)) / 1000;
        String l2 = "TX in " + String(rem) + "s";
        String l3 = "#" + String(uplinkCount) + " " + lastStatus;
        drawStatus("JOINED OK", l2.c_str(), l3.c_str());
    } else {
        // Kein Join – Beacon + Bidir läuft
        String l2 = "BCN:#" + String(beaconCount);
        drawStatus("No TTN Join", l2.c_str(), "NEAR_FIND+ACK");

        // Retry Join alle 180s
        if (now - lastUplink > 180000) {
            lastUplink = now;
            joinRetryCount++;
            Serial.printf("[LORA] Retry Join #%lu...\n", joinRetryCount);

            if (joinRetryCount == 5) {
                Serial.println("[LORA] Loessche NVS Session");
                prefs.remove("session");
            }

            lastBeacon = millis();  // kein Beacon während Join-Fenster

            int16_t rc = node->activateOTAA(0);
            prefs.putBytes("nonces", node->getBufferNonces(), RADIOLIB_LORAWAN_NONCES_BUF_SIZE);

            if (rc == RADIOLIB_LORAWAN_NEW_SESSION ||
                rc == RADIOLIB_LORAWAN_SESSION_RESTORED ||
                rc == RADIOLIB_ERR_NONE) {
                joined = true;
                prefs.putBytes("session", node->getBufferSession(), RADIOLIB_LORAWAN_SESSION_BUF_SIZE);
                Serial.printf("[LORA] Join OK (Retry#%lu, rc=%d)!\n", joinRetryCount, rc);
                drawStatus("JOINED!", "TTN OK", "");
            } else {
                Serial.printf("[LORA] Retry Fehler: %d\n", rc);
                drawStatus("Join Fehler", ("Err:" + String(rc)).c_str(),
                           ("Retry#" + String(joinRetryCount)).c_str());
            }
        }
    }

    // --- Config speichern wenn dirty (max. alle 10s) ---
    static uint32_t lastConfigSave = 0;
    if (configDirty && (now - lastConfigSave > 10000)) {
        lastConfigSave = now;
        saveConfig();
    }

    // Heartbeat alle 5s
    if (now - lastHeartbeat > 5000) {
        lastHeartbeat = now;
        Serial.printf("[HB] %s joined=%d bcn=%lu ack=%lu gwRSSI=%d bat=%dmV state=%s evd=%d wifi=%s\n",
            FIRMWARE_VERSION, joined, beaconCount, ackCount, lastGwRssi,
            readBatMv(), stateName(currentState),
            computeEvidenceScore(),
            cfg.wifiEnabled ? (wifiInZone ? "IN" : (wifiScanDone ? "OUT" : "?")) : "OFF");
        if (cfg.gpsEnabled) {
            if (hasGpsFix()) {
                Serial.printf("[GPS] Fix: %.6f,%.6f alt=%.0fm sats=%d hdop=%.1f\n",
                              lastGpsFix.lat, lastGpsFix.lon,
                              lastGpsFix.altM, lastGpsFix.sats, lastGpsFix.hdop);
            } else {
                Serial.printf("[GPS] Kein Fix | chars=%lu sats=%u\n",
                              (unsigned long)gps.charsProcessed(),
                              gps.satellites.isValid() ? gps.satellites.value() : 0);
            }
        }
        Serial.flush();
    }

    delay(50);
}
