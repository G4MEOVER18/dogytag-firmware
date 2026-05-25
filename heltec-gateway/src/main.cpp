// =============================================================
// SmartTag HeltecTracker v1.2 – Gateway Firmware v2.5.3
// Hardware: Heltec Wireless Tracker v1.2
//   Display: ST7735 TFT 160x80
//   LoRa:    SX1262 868 MHz EU
//   WiFi:    ESP32-S3 integriert
//
// Funktion:
//   - NEAR_FIND: Raw LoRa Empfang + ACK zurück (bidirektional)
//   - empfangene Pakete + Log → MQTT (lokal 192.168.0.80:1883)
//   - MQTT-Offline-Queue: bis zu 20 Nachrichten puffern
//   - Eigenstatus (WiFi, MQTT, LoRa) auf TFT anzeigen
//   - Tracker-Infos (RSSI, Bat, Counter) auf TFT anzeigen
//   - Test-TX via GPIO0 (Boot-Taste) oder MQTT-Befehl
//
// Paket-Typen (Byte 0):
//   0x42 'B' = Beacon  (Tag→GW, 11B, kompatibel mit v2.x)
//   0x41 'A' = ACK     (GW→Tag, 9B)
//   0x4C 'L' = Log     (Tag→GW, 12B)
//   0xAB     = alter Beacon-Marker (letztes Byte, Typ-Erkennung via len+Byte0)
// =============================================================

#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <RadioLib.h>
#include <TinyGPS++.h>
#include <time.h>
#include "HT_st7735.h"
#include "secrets.h"
#include "command_ids.h"

// =============================================================
// HARDWARE PINS
// =============================================================
// STATUS_LED: Wireless Tracker v1.2 hat keine User-LED (GPIO25 = ungültig auf ESP32-S3)
constexpr uint8_t BTN_TX_TEST = 0;   // Boot/PRG Taste, active LOW

// SX1262 (HSPI: SCK=9, MISO=11, MOSI=10)
constexpr uint8_t LORA_NSS  = 8;
constexpr uint8_t LORA_DIO1 = 14;
constexpr uint8_t LORA_RST  = 12;
constexpr uint8_t LORA_BUSY = 13;

// =============================================================
// LORA PARAMETER
// =============================================================
constexpr float   LORA_FREQ_MHZ = 868.0f;
constexpr float   LORA_BW_KHZ  = 125.0f;
constexpr uint8_t LORA_SF      = 7;
constexpr uint8_t LORA_CR      = 5;
constexpr uint8_t LORA_SYNC    = 0xAB;
constexpr int8_t  LORA_TX_PWR  = 14;

// =============================================================
// PAKET-TYPEN
// =============================================================
constexpr uint8_t PKT_ACK_GW = 0x41;  // 'A'
constexpr uint8_t PKT_LOG_GW = 0x4C;  // 'L'
// CMD/RESP via command_ids.h

// =============================================================
// PENDING COMMAND QUEUE (Gateway → Tracker)
// =============================================================
struct PendingCmd {
    char    deviceId[16];
    char    cmdIdStr[48];     // MQTT cmdId String für ACK
    char    cmdName[32];      // MQTT cmd Name für ACK
    uint8_t frame[64];
    uint8_t frameLen;
    uint32_t queuedAt;
    uint32_t ttlMs;            // Ablaufzeit in ms ab queuedAt
    bool    active;
};
constexpr uint8_t PENDING_CMD_SLOTS = 4;
static PendingCmd pendingCmds[PENDING_CMD_SLOTS];

// Pending Command einfügen (ttlSec: TTL in Sekunden, default 30)
static bool enqueuePendingCmd(const char* devId, const char* cmdIdStr,
                               const char* cmdName,
                               const uint8_t* frame, uint8_t flen,
                               uint32_t ttlSec = 30) {
    for (int i = 0; i < PENDING_CMD_SLOTS; i++) {
        if (!pendingCmds[i].active) {
            strncpy(pendingCmds[i].deviceId, devId,    sizeof(pendingCmds[i].deviceId)-1);
            strncpy(pendingCmds[i].cmdIdStr, cmdIdStr, sizeof(pendingCmds[i].cmdIdStr)-1);
            strncpy(pendingCmds[i].cmdName,  cmdName,  sizeof(pendingCmds[i].cmdName)-1);
            memcpy(pendingCmds[i].frame, frame, flen);
            pendingCmds[i].frameLen  = flen;
            pendingCmds[i].queuedAt  = millis();
            pendingCmds[i].ttlMs     = ttlSec * 1000UL;
            pendingCmds[i].active    = true;
            Serial.printf("[CMD] Queued '%s' für %s (TTL=%lus)\n",
                          cmdName, devId, (unsigned long)ttlSec);
            return true;
        }
    }
    Serial.println("[CMD] Queue voll!");
    return false;
}

// Naechsten Pending Command für Device holen (NULL wenn keiner)
static PendingCmd* getPendingCmd(const char* devId) {
    for (int i = 0; i < PENDING_CMD_SLOTS; i++) {
        if (pendingCmds[i].active &&
            strncmp(pendingCmds[i].deviceId, devId, 15) == 0) {
            return &pendingCmds[i];
        }
    }
    return nullptr;
}

// Pending Command abräumen (nach RESP oder Timeout)
static void clearPendingCmd(PendingCmd* cmd) {
    if (cmd) cmd->active = false;
}

// Timeouts aufräumen (nach individuellem TTL)
static void expirePendingCmds() {
    uint32_t now = millis();
    for (int i = 0; i < PENDING_CMD_SLOTS; i++) {
        if (pendingCmds[i].active &&
            (now - pendingCmds[i].queuedAt > pendingCmds[i].ttlMs)) {
            Serial.printf("[CMD] TTL abgelaufen: '%s' für %s\n",
                          pendingCmds[i].cmdName, pendingCmds[i].deviceId);
            pendingCmds[i].active = false;
        }
    }
}

// =============================================================
// GATEWAY GPS (onboard UC6580, auf Abruf)
// UART1: GPIO33=RX (← UC6580 TX), GPIO34=TX (→ UC6580 RX)
// Power: VGNSS_CTRL=GPIO3 already LOW (on) since setup()
// =============================================================
constexpr uint8_t  GW_GPS_RX_PIN    = 33;
constexpr uint8_t  GW_GPS_TX_PIN    = 34;
constexpr uint32_t GW_GPS_BAUD      = 9600;
constexpr uint16_t GW_GPS_TIMEOUT_S = 120;

HardwareSerial serialGwGps(1);   // UART1
TinyGPSPlus    gwGps;
static bool     gwGpsActive  = false;
static uint32_t gwGpsStartMs = 0;
static bool     gwGpsHasFix  = false;

// =============================================================
// TIMING
// =============================================================
constexpr uint32_t STATUS_INTERVAL_MS    = 30000;
constexpr uint32_t TELEMETRY_INTERVAL_MS = 60000;
constexpr uint32_t DISPLAY_INTERVAL_MS  = 2000;
constexpr uint32_t WIFI_RETRY_MS        = 15000;
constexpr uint32_t MQTT_RETRY_MS        = 8000;
constexpr uint32_t TRACKER_TIMEOUT_MS   = 120000;
constexpr uint32_t BTN_DEBOUNCE_MS      = 60;

// =============================================================
// MQTT OFFLINE QUEUE
// =============================================================
constexpr uint8_t MQTT_QUEUE_SIZE = 20;

struct MqttQueueEntry {
    char topic[48];
    char payload[256];
};

MqttQueueEntry mqttQueue[MQTT_QUEUE_SIZE];
uint8_t mqttQHead  = 0;
uint8_t mqttQCount = 0;

// =============================================================
// TRACKER REGISTRY
// =============================================================
constexpr int MAX_TRACKERS = 4;

struct TrackerInfo {
    char     id[8];
    int16_t  rssi;
    float    snr;
    uint16_t batMv;
    uint16_t counter;
    uint32_t lastSeen;
    bool     valid;
};

TrackerInfo trackers[MAX_TRACKERS];
int         lastTrackerIdx = -1;

// =============================================================
// 433MHz MODEM (CubeCell HTCC-AB02A via UART2)
// Gateway GPIO4 = RX2 ← CubeCell TX1
// Gateway GPIO5 = TX2 → CubeCell RX1
// =============================================================
constexpr uint8_t  MODEM433_RX_PIN  = 4;
constexpr uint8_t  MODEM433_TX_PIN  = 5;
constexpr uint32_t MODEM433_BAUD    = 115200;

HardwareSerial serial433(2);   // UART2

static char   m433CmdBuf[160];
static uint8_t m433CmdPos = 0;
static bool     m433Ready     = false;
static uint32_t rx433Count    = 0;
static uint32_t tx433Count    = 0;
static uint32_t last433PingMs = 0;   // letzter STATUS-Ping (nicht bereit: 5s, bereit: 60s)
static char     m433StatusBuf[128];  // letzte STATUS-Antwort vom Modem

// Bytes → Hex-String
static void bytesToHex433(const uint8_t* data, size_t len, char* out) {
    for (size_t i = 0; i < len; i++) sprintf(out + i * 2, "%02X", data[i]);
    out[len * 2] = '\0';
}

// Hex-String → Bytes; gibt Länge zurück oder -1 bei Fehler
static int hexToBytes433(const char* hex, uint8_t* out, size_t maxLen) {
    size_t hexLen = strlen(hex);
    if (hexLen == 0 || hexLen % 2 != 0) return -1;
    size_t byteLen = hexLen / 2;
    if (byteLen > maxLen) return -1;
    for (size_t i = 0; i < byteLen; i++) {
        char b[3] = { hex[i*2], hex[i*2+1], '\0' };
        out[i] = (uint8_t)strtol(b, nullptr, 16);
    }
    return (int)byteLen;
}

// Paket über 433MHz senden (Impl. nach Global-Objekte, vor setup)
void send433(const uint8_t* data, size_t len);
void handle433RxLine(const char* line);
void poll433();

// =============================================================
// GLOBALE OBJEKTE
// =============================================================
SPIClass    loraSPI(FSPI);   // FSPI=SPI2, HSPI=SPI3 ist für TFT reserviert
SX1262*     radio = nullptr;
volatile bool loraPacketReady = false;  // IRQ-Flag für RX-Interrupt
HT_st7735   tft;
WiFiClient  wifiClient;
PubSubClient mqtt(wifiClient);

// =============================================================
// STATE
// =============================================================
bool     radioReady    = false;
bool     wifiOK        = false;
bool     mqttOK        = false;
bool     ntpSynced     = false;
bool     testTxPending = false;

uint32_t rxCount    = 0;
uint32_t rxErrors   = 0;
uint32_t txCount    = 0;
uint32_t ackCount   = 0;   // Gesendete ACKs
uint32_t bootTime   = 0;

char     lastCmdDevId[16] = "";  // letztes Gerät, an das ein CMD gesendet wurde

uint32_t lastStatusTs    = 0;
uint32_t lastTelemetryTs = 0;
uint32_t lastDisplayTs   = 0;
uint32_t lastWifiRetry   = 0;
uint32_t lastMqttRetry   = 0;

// --- Display-Seiten ---
constexpr uint8_t  TOTAL_PAGES   = 3;
constexpr uint32_t PAGE_CYCLE_MS = 8000;
constexpr uint32_t BTN_LONG_MS   = 800;
uint8_t  currentPage    = 0;
uint32_t lastPageChange = 0;

// --- Button ---
bool     lastBtnState = HIGH;
uint32_t btnPressedAt = 0;
bool     btnLongFired = false;

// =============================================================
// LORA RX INTERRUPT
// =============================================================
void IRAM_ATTR onLoraPacket() {
    loraPacketReady = true;
}

static void startRx() {
    int16_t rc = radio->startReceive();
    if (rc != RADIOLIB_ERR_NONE) {
        Serial.printf("[LORA] startReceive ERR: %d\n", rc);
        Serial.flush();
    }
}

// =============================================================
// HILFSFUNKTIONEN
// =============================================================
uint32_t getTimestamp() {
    if (ntpSynced) return (uint32_t)time(nullptr);
    return (uint32_t)(millis() / 1000);
}

int rssiToBars(int16_t rssi) {
    if (rssi >= -60)  return 5;
    if (rssi >= -70)  return 4;
    if (rssi >= -80)  return 3;
    if (rssi >= -90)  return 2;
    if (rssi >= -100) return 1;
    return 0;
}

const char* rssiToQuality(int16_t rssi) {
    if (rssi >= -70)  return "STARK  ";
    if (rssi >= -100) return "MITTEL ";
    return "SCHWACH";
}

uint16_t rssiToColor(int16_t rssi) {
    if (rssi >= -70)  return ST7735_GREEN;
    if (rssi >= -100) return ST7735_YELLOW;
    return ST7735_RED;
}

// =============================================================
// MQTT QUEUE – gepuffertes Publizieren
// =============================================================
bool queuedPublish(const char* topic, const char* payload) {
    if (mqttOK && mqtt.connected()) {
        return mqtt.publish(topic, payload);
    }
    if (mqttQCount < MQTT_QUEUE_SIZE) {
        uint8_t idx = (mqttQHead + mqttQCount) % MQTT_QUEUE_SIZE;
        strncpy(mqttQueue[idx].topic,   topic,   sizeof(mqttQueue[idx].topic) - 1);
        strncpy(mqttQueue[idx].payload, payload, sizeof(mqttQueue[idx].payload) - 1);
        mqttQueue[idx].topic[sizeof(mqttQueue[idx].topic) - 1]     = '\0';
        mqttQueue[idx].payload[sizeof(mqttQueue[idx].payload) - 1] = '\0';
        mqttQCount++;
        Serial.printf("[Q] +%u/%u  %s\n", mqttQCount, MQTT_QUEUE_SIZE, topic);
        return true;
    }
    Serial.println("[Q] Voll – Nachricht verworfen");
    return false;
}

void flushMqttQueue() {
    if (mqttQCount == 0) return;
    uint8_t flushed = 0;
    while (mqttQCount > 0 && mqtt.connected()) {
        MqttQueueEntry& e = mqttQueue[mqttQHead];
        mqtt.publish(e.topic, e.payload);
        Serial.printf("[Q] Flush: %s\n", e.topic);
        mqttQHead = (mqttQHead + 1) % MQTT_QUEUE_SIZE;
        mqttQCount--;
        flushed++;
        mqtt.loop();
        delay(30);
    }
    if (mqttQCount == 0) mqttQHead = 0;
    Serial.printf("[Q] %u Nachrichten geflusht\n", flushed);
}

// =============================================================
// TRACKER REGISTRY
// =============================================================
int findOrAddTracker(const char* id) {
    for (int i = 0; i < MAX_TRACKERS; i++) {
        if (trackers[i].valid && strcmp(trackers[i].id, id) == 0) return i;
    }
    for (int i = 0; i < MAX_TRACKERS; i++) {
        if (!trackers[i].valid) {
            strncpy(trackers[i].id, id, sizeof(trackers[i].id) - 1);
            trackers[i].id[sizeof(trackers[i].id) - 1] = '\0';
            return i;
        }
    }
    int oldest = 0;
    for (int i = 1; i < MAX_TRACKERS; i++) {
        if (trackers[i].lastSeen < trackers[oldest].lastSeen) oldest = i;
    }
    strncpy(trackers[oldest].id, id, sizeof(trackers[oldest].id) - 1);
    trackers[oldest].id[sizeof(trackers[oldest].id) - 1] = '\0';
    trackers[oldest].valid = false;
    return oldest;
}

void expireTrackers() {
    uint32_t now = millis();
    for (int i = 0; i < MAX_TRACKERS; i++) {
        if (trackers[i].valid && (now - trackers[i].lastSeen > TRACKER_TIMEOUT_MS)) {
            Serial.printf("[REG] Tracker %s abgelaufen\n", trackers[i].id);
            trackers[i].valid = false;
            if (lastTrackerIdx == i) {
                lastTrackerIdx = -1;
                for (int j = 0; j < MAX_TRACKERS; j++) {
                    if (trackers[j].valid) { lastTrackerIdx = j; break; }
                }
            }
        }
    }
}

int activeTrackerCount() {
    int n = 0;
    for (int i = 0; i < MAX_TRACKERS; i++) if (trackers[i].valid) n++;
    return n;
}

// =============================================================
// PAKET PARSEN
// Unterstützt altes Format (11B) und neues Format (12B mit Typ-Byte)
// =============================================================
bool parsePacket(const uint8_t* data, size_t len, TrackerInfo* out) {
    const uint8_t* id_ptr;
    const uint8_t* cnt_ptr;
    const uint8_t* bat_ptr;
    uint8_t        marker;

    if (len == 12 && (data[0] == 0x42 || data[0] < 0x20)) {
        // Neues Format: [TYPE][ID 6B][CNT 2B][BAT 2B][0xAB]
        id_ptr  = data + 1;
        cnt_ptr = data + 7;
        bat_ptr = data + 9;
        marker  = data[11];
    } else if (len >= 11) {
        // Altes Format: [ID 6B][CNT 2B][BAT 2B][0xAB]
        id_ptr  = data;
        cnt_ptr = data + 6;
        bat_ptr = data + 8;
        marker  = data[10];
    } else {
        return false;
    }

    if (marker != 0xAB && marker != 0x7F) return false;

    memset(out->id, 0, sizeof(out->id));
    for (int i = 0; i < 6; i++) {
        if (id_ptr[i] < 0x20 || id_ptr[i] > 0x7E) break;
        out->id[i] = (char)id_ptr[i];
    }
    if (out->id[0] == '\0') return false;

    out->counter = ((uint16_t)cnt_ptr[0] << 8) | cnt_ptr[1];
    out->batMv   = ((uint16_t)bat_ptr[0] << 8) | bat_ptr[1];
    return true;
}

// =============================================================
// LOG-PAKET VOM TAG VERARBEITEN
// Format: [0x4C][SRC_ID 6B][logSeq 2B][batMv 2B][0xAB] = 12 Bytes
// =============================================================
void handleLogPacket(const uint8_t* data, size_t len, int16_t rssi, float snr) {
    if (len < 12) return;
    if (data[11] != 0xAB) return;

    char srcId[8] = {0};
    for (int i = 0; i < 6; i++) {
        if (data[1 + i] < 0x20 || data[1 + i] > 0x7E) break;
        srcId[i] = (char)data[1 + i];
    }
    if (srcId[0] == '\0') return;

    uint16_t logSeq = ((uint16_t)data[7] << 8) | data[8];
    uint16_t batMv  = ((uint16_t)data[9] << 8) | data[10];

    Serial.printf("[LOG] %s seq=%u bat=%dmV GW-RSSI=%d\n",
                  srcId, logSeq, batMv, rssi);

    char topic[48];
    snprintf(topic, sizeof(topic), "dogytag/%s/log", srcId);

    StaticJsonDocument<256> doc;
    doc["device_id"]  = srcId;
    doc["timestamp"]  = getTimestamp();
    doc["log_seq"]    = logSeq;
    doc["battery_mv"] = batMv;
    doc["gw_rssi"]    = rssi;
    doc["gw_snr"]     = (float)((int)(snr * 10)) / 10.0f;
    doc["gateway"]    = DEVICE_ID;

    char buf[256];
    serializeJson(doc, buf, sizeof(buf));
    queuedPublish(topic, buf);
}

// =============================================================
// GPS-PAKET VOM TAG VERARBEITEN
// Format: [PKT_GPS][ID 6B][lat 4B][lon 4B][alt 2B][sats 1B][hdop 1B][0xAB] = 20B
// =============================================================
void handleGpsPacket(const uint8_t* buf, size_t len, int16_t rssi, float snr) {
    if (len < 20) return;
    if (buf[19] != 0xAB) return;

    char srcId[8] = {};
    for (int i = 0; i < 6; i++) {
        if (buf[1 + i] < 0x20 || buf[1 + i] > 0x7E) break;
        srcId[i] = (char)buf[1 + i];
    }
    if (srcId[0] == '\0') return;

    int32_t lat_i = ((int32_t)buf[7]  << 24) | ((int32_t)buf[8]  << 16)
                  | ((int32_t)buf[9]  <<  8) |  (int32_t)buf[10];
    int32_t lon_i = ((int32_t)buf[11] << 24) | ((int32_t)buf[12] << 16)
                  | ((int32_t)buf[13] <<  8) |  (int32_t)buf[14];
    int16_t alt_i = ((int16_t)buf[15] << 8) | buf[16];
    uint8_t sats  = buf[17];
    float   hdop  = buf[18] / 10.0f;
    double  lat   = lat_i / 1e6;
    double  lon   = lon_i / 1e6;

    // hdop=255 → Searching (kein Fix), sonst Fix vorhanden
    bool hasFix = (buf[18] != 255);

    if (hasFix) {
        Serial.printf("[GPS] %s FIX %.6f,%.6f alt=%dm sats=%d hdop=%.1f RSSI:%d\n",
                      srcId, lat, lon, (int)alt_i, sats, hdop, rssi);
    } else {
        Serial.printf("[GPS] %s SEARCHING sats=%d RSSI:%d\n", srcId, sats, rssi);
    }

    char topic[48];
    snprintf(topic, sizeof(topic), "dogytag/%s/gps", srcId);

    StaticJsonDocument<256> doc;
    doc["device_id"] = srcId;
    doc["timestamp"] = getTimestamp();
    doc["fix"]       = hasFix;
    doc["lat"]       = hasFix ? lat : 0.0;
    doc["lon"]       = hasFix ? lon : 0.0;
    doc["alt_m"]     = hasFix ? (int)alt_i : 0;
    doc["sats"]      = sats;
    doc["hdop"]      = hasFix ? hdop : 0.0f;
    doc["rssi"]      = rssi;
    doc["snr"]       = (float)((int)(snr * 10)) / 10.0f;
    doc["gateway"]   = DEVICE_ID;

    char mbuf[256];
    serializeJson(doc, mbuf, sizeof(mbuf));
    queuedPublish(topic, mbuf);
}

// =============================================================
// CMD AN TRACKER SENDEN (nach ACK, falls Queue-Eintrag vorhanden)
// Routing-Priorität: 433MHz (CubeCell) → 868MHz
// =============================================================
void sendCmdToTracker(const char* devId) {
    PendingCmd* pending = getPendingCmd(devId);
    if (!pending) return;

    Serial.printf("[CMD→] '%s' → %s (%uB)\n",
                  pending->cmdName, devId, pending->frameLen);

    uint16_t seq = ((uint16_t)pending->frame[3] << 8) | pending->frame[4];
    bool sent    = false;
    const char* via = "none";

    // 1) Versuch: 433MHz via CubeCell (niedrige Latenz, lokaler Transport)
    if (m433Ready && !sent) {
        send433(pending->frame, pending->frameLen);
        tx433Count++;
        sent = true;
        via  = "433";
        Serial.printf("[CMD→] via 433 seq=%u\n", seq);
    }

    // 2) Fallback: 868MHz direkt
    if (!sent) {
        int16_t rc = radio->transmit(pending->frame, pending->frameLen);
        loraPacketReady = false;
        if (rc == RADIOLIB_ERR_NONE) {
            txCount++;
            sent = true;
            via  = "868";
            Serial.printf("[CMD→] via 868 seq=%u\n", seq);
        } else {
            Serial.printf("[CMD→] 868 TX Fehler %d\n", rc);
        }
    }

    if (sent) {
        strncpy(lastCmdDevId, devId, sizeof(lastCmdDevId) - 1);

        char fwdTopic[72];
        snprintf(fwdTopic, sizeof(fwdTopic), "dogytag/gateway/forwarded/%s", devId);
        StaticJsonDocument<192> doc;
        doc["cmd"]       = pending->cmdName;
        doc["cmdId"]     = pending->cmdIdStr;
        doc["target"]    = devId;
        doc["via"]       = via;
        doc["seq"]       = seq;
        doc["timestamp"] = getTimestamp();
        char buf[192];
        serializeJson(doc, buf, sizeof(buf));
        queuedPublish(fwdTopic, buf);
    }
    clearPendingCmd(pending);
}

// =============================================================
// RESP-PAKET VOM TAG VERARBEITEN
// Format: [PKT_RESP][PROTO_VER][SEQ_HI][SEQ_LO][CMD_ID][RC][LEN][DATA…][CRC_HI][CRC_LO][0xAB]
// =============================================================
void handleRespPacket(const uint8_t* buf, size_t len, int16_t rssi, float snr) {
    if (len < 10) return;
    if (buf[1] != PROTO_VERSION) return;

    uint16_t seq        = ((uint16_t)buf[2] << 8) | buf[3];
    uint8_t  cmdId      = buf[4];
    uint8_t  resultCode = buf[5];
    uint8_t  respLen    = buf[6];

    if (len < (size_t)(7 + respLen + 3)) return;

    // CRC prüfen
    uint16_t rxCrc   = ((uint16_t)buf[7 + respLen] << 8) | buf[8 + respLen];
    uint16_t calcCrc = crc16(buf, 7 + respLen);
    if (rxCrc != calcCrc) {
        Serial.printf("[RESP] CRC Fehler seq=%u rx=%04X calc=%04X\n",
                      seq, rxCrc, calcCrc);
        return;
    }
    if (buf[9 + respLen] != FRAME_END_MARKER) return;

    char respData[49] = "";
    if (respLen > 0 && respLen <= 48) {
        memcpy(respData, buf + 7, respLen);
        respData[respLen] = '\0';
    }

    Serial.printf("[RESP] seq=%u cmd=0x%02X rc=%u data='%s' RSSI:%d\n",
                  seq, cmdId, resultCode, respData, rssi);

    // deviceId: letztes CMD-Target (lastCmdDevId), fallback: letzter Tracker
    const char* devId = lastCmdDevId[0] ? lastCmdDevId
                        : (lastTrackerIdx >= 0 ? trackers[lastTrackerIdx].id : nullptr);
    if (!devId) return;

    char ackTopic[64];
    snprintf(ackTopic, sizeof(ackTopic), "dogytag/%s/ack", devId);

    StaticJsonDocument<256> doc;
    doc["seq"]        = seq;
    doc["cmdId"]      = cmdId;
    doc["result"]     = (resultCode == RC_OK) ? "OK" : "ERR";
    doc["resultCode"] = resultCode;
    doc["data"]       = respData;
    doc["rssi"]       = rssi;
    doc["snr"]        = (float)((int)(snr * 10)) / 10.0f;
    doc["timestamp"]  = getTimestamp();
    char mbuf[256];
    serializeJson(doc, mbuf, sizeof(mbuf));
    queuedPublish(ackTopic, mbuf);
}

// =============================================================
// GATEWAY GPS FUNKTIONEN
// Trigger:  MQTT dogytag/gateway/cmd {"cmd":"GPS_TRIGGER"}
// Stopp:    MQTT dogytag/gateway/cmd {"cmd":"GPS_STOP"}
// Ergebnis: MQTT dogytag/gateway/gps (fix=true/false)
// =============================================================
static void publishGwGps(bool fix) {
    StaticJsonDocument<256> doc;
    doc["gateway"]   = DEVICE_ID;
    doc["timestamp"] = getTimestamp();
    doc["fix"]       = fix;
    if (fix) {
        doc["lat"]   = gwGps.location.lat();
        doc["lon"]   = gwGps.location.lng();
        doc["alt_m"] = (int)gwGps.altitude.meters();
        doc["sats"]  = gwGps.satellites.isValid() ? (int)gwGps.satellites.value() : 0;
        doc["hdop"]  = (float)((int)(gwGps.hdop.hdop() * 10)) / 10.0f;
        doc["age_ms"] = (int)gwGps.location.age();
    } else {
        doc["sats"]  = gwGps.satellites.isValid() ? (int)gwGps.satellites.value() : 0;
        doc["chars"] = (int)gwGps.charsProcessed();
    }
    char buf[256];
    serializeJson(doc, buf, sizeof(buf));
    queuedPublish("dogytag/gateway/gps", buf);
}

void startGatewayGps() {
    if (gwGpsActive) {
        Serial.println("[GW-GPS] Laeuft bereits");
        return;
    }
    serialGwGps.begin(GW_GPS_BAUD, SERIAL_8N1, GW_GPS_RX_PIN, GW_GPS_TX_PIN);
    gwGpsActive  = true;
    gwGpsStartMs = millis();
    gwGpsHasFix  = false;
    Serial.printf("[GW-GPS] Start – RX=GPIO%d TX=GPIO%d @%lu baud\n",
                  GW_GPS_RX_PIN, GW_GPS_TX_PIN, (unsigned long)GW_GPS_BAUD);
    queuedPublish("dogytag/gateway/gps_status",
                  "{\"status\":\"searching\",\"timeout_s\":120}");
}

void stopGatewayGps(bool publishResult = true) {
    if (!gwGpsActive) return;
    if (publishResult) publishGwGps(gwGpsHasFix);
    serialGwGps.end();
    gwGpsActive = false;
    Serial.printf("[GW-GPS] Gestoppt – fix=%s chars=%lu\n",
                  gwGpsHasFix ? "JA" : "NEIN",
                  (unsigned long)gwGps.charsProcessed());
}

void pollGatewayGps() {
    if (!gwGpsActive) return;

    while (serialGwGps.available()) {
        gwGps.encode(serialGwGps.read());
    }

    // Fix: gültige Position + HDOP < 5
    if (gwGps.location.isValid() && gwGps.location.isUpdated()
        && gwGps.hdop.isValid() && gwGps.hdop.hdop() < 5.0f) {
        gwGpsHasFix = true;
        Serial.printf("[GW-GPS] FIX: %.6f,%.6f alt=%.1fm sats=%d hdop=%.1f\n",
                      gwGps.location.lat(), gwGps.location.lng(),
                      gwGps.altitude.meters(),
                      gwGps.satellites.isValid() ? (int)gwGps.satellites.value() : 0,
                      gwGps.hdop.hdop());
        stopGatewayGps(true);
        return;
    }

    // Timeout
    if (millis() - gwGpsStartMs > (uint32_t)GW_GPS_TIMEOUT_S * 1000UL) {
        Serial.printf("[GW-GPS] Timeout %ds – kein Fix (chars=%lu sats=%d)\n",
                      GW_GPS_TIMEOUT_S,
                      (unsigned long)gwGps.charsProcessed(),
                      gwGps.satellites.isValid() ? (int)gwGps.satellites.value() : 0);
        stopGatewayGps(true);
    }
}

// =============================================================
// ACK SENDEN (GW → Tag)
// Format: [0x41][TARGET_ID 6B][RSSI i8][SNR*4 i8] = 9 Bytes
// Nach dem ACK: offene CMDs in Queue an Tracker senden
// =============================================================
void sendAck(const char* targetId, int16_t rxRssi, float rxSnr) {
    uint8_t pkt[9] = {0};
    pkt[0] = PKT_ACK_GW;
    size_t idLen = strnlen(targetId, 6);
    for (size_t i = 0; i < idLen; i++) pkt[1 + i] = (uint8_t)targetId[i];
    pkt[7] = (uint8_t)((int8_t)constrain((int)rxRssi, -128, 0));
    pkt[8] = (uint8_t)((int8_t)constrain((int)(rxSnr * 4.0f), -128, 127));

    int16_t rc = radio->transmit(pkt, sizeof(pkt));
    loraPacketReady = false;  // TX_DONE fires DIO1 → clears spurious RX IRQ flag
    if (rc == RADIOLIB_ERR_NONE) {
        txCount++;
        ackCount++;
        Serial.printf("[ACK] → %s RSSI=%d SNR=%.1f\n", targetId, rxRssi, rxSnr);
    } else {
        Serial.printf("[ACK] TX Fehler: %d\n", (int)rc);
    }

    // CMD aus Queue senden (Tag öffnet 400ms CMD-Fenster nach ACK)
    sendCmdToTracker(targetId);

    startRx();   // nach TX wieder in RX-Modus
}

// =============================================================
// MQTT PUBLISH (alle über Queue-Wrapper)
// =============================================================
void publishStatus() {
    char topic[48];
    snprintf(topic, sizeof(topic), "dogytag/%s/status", DEVICE_ID);

    StaticJsonDocument<256> doc;
    doc["device_id"] = DEVICE_ID;
    doc["timestamp"] = getTimestamp();
    doc["state"]     = "online";
    doc["mode"]      = "GATEWAY";
    doc["wifi_rssi"] = wifiOK ? (int)WiFi.RSSI() : 0;
    doc["uptime_s"]  = (unsigned long)((millis() - bootTime) / 1000);

    char buf[256];
    serializeJson(doc, buf, sizeof(buf));
    queuedPublish(topic, buf);
    Serial.printf("[MQTT] %s\n", topic);
}

void publishTelemetry() {
    char topic[48];
    snprintf(topic, sizeof(topic), "dogytag/%s/telemetry", DEVICE_ID);

    StaticJsonDocument<384> doc;
    doc["device_id"] = DEVICE_ID;
    doc["timestamp"] = getTimestamp();
    doc["rx_count"]  = (unsigned long)rxCount;
    doc["tx_count"]  = (unsigned long)txCount;
    doc["ack_count"] = (unsigned long)ackCount;
    doc["rx_errors"] = (unsigned long)rxErrors;
    doc["wifi_rssi"] = wifiOK ? (int)WiFi.RSSI() : 0;
    doc["heap_free"] = (unsigned long)ESP.getFreeHeap();
    doc["uptime_s"]  = (unsigned long)((millis() - bootTime) / 1000);
    doc["trackers"]  = activeTrackerCount();
    doc["q_pending"]  = mqttQCount;
    doc["rx433_count"]= (unsigned long)rx433Count;
    doc["tx433_count"]= (unsigned long)tx433Count;
    doc["modem433"]   = m433Ready ? "online" : "offline";
    doc["mode"]       = "GATEWAY";

    char buf[384];
    serializeJson(doc, buf, sizeof(buf));
    queuedPublish(topic, buf);
    Serial.printf("[MQTT] %s\n", topic);

    // Separates Modem-433-Topic mit Detailstatus
    StaticJsonDocument<256> mdoc;
    mdoc["gateway"]    = DEVICE_ID;
    mdoc["timestamp"]  = getTimestamp();
    mdoc["ready"]      = m433Ready;
    mdoc["rx_count"]   = (unsigned long)rx433Count;
    mdoc["tx_count"]   = (unsigned long)tx433Count;
    mdoc["last_status"] = m433StatusBuf[0] ? m433StatusBuf : "none";
    char mbuf[256];
    serializeJson(mdoc, mbuf, sizeof(mbuf));
    queuedPublish("dogytag/gateway/modem433", mbuf);
}

void publishLoraPacket(const TrackerInfo& tr) {
    char topic[48];
    snprintf(topic, sizeof(topic), "dogytag/%s/lora", tr.id);

    StaticJsonDocument<256> doc;
    doc["device_id"]  = tr.id;
    doc["timestamp"]  = getTimestamp();
    doc["rssi"]       = tr.rssi;
    doc["snr"]        = (float)((int)(tr.snr * 10)) / 10.0f;
    doc["gateway"]    = DEVICE_ID;
    doc["counter"]    = tr.counter;
    doc["battery_mv"] = tr.batMv;

    char buf[256];
    serializeJson(doc, buf, sizeof(buf));
    bool ok = queuedPublish(topic, buf);
    Serial.printf("[MQTT] %s %s rssi=%d\n", topic, ok ? "OK" : "Q", tr.rssi);
}

void publishEvent(const char* event, const char* msg, const char* severity = "info") {
    char topic[48];
    snprintf(topic, sizeof(topic), "dogytag/%s/event", DEVICE_ID);

    StaticJsonDocument<256> doc;
    doc["device_id"] = DEVICE_ID;
    doc["timestamp"] = getTimestamp();
    doc["event"]     = event;
    doc["message"]   = msg;
    doc["severity"]  = severity;

    char buf[256];
    serializeJson(doc, buf, sizeof(buf));
    queuedPublish(topic, buf);
}

// =============================================================
// MQTT CALLBACK
// Empfängt Commands von dogytag/+/cmd (alle Tracker)
// und dogytag/<gwId>/cmd (Gateway-Self-Commands)
//
// JSON-Format:
//   { "cmdId": "<uuid>", "cmd": "SET_BEACON_INTERVAL",
//     "params": { "value": 30 },
//     "requiresAck": true, "priority": "normal", "ttl": 30 }
// =============================================================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
    StaticJsonDocument<512> doc;
    if (deserializeJson(doc, payload, length)) {
        Serial.println("[CMD] JSON Parse-Fehler");
        return;
    }

    // deviceId aus Topic extrahieren: dogytag/<deviceId>/cmd
    char devId[32] = "";
    {
        char tCopy[80];
        strncpy(tCopy, topic, sizeof(tCopy) - 1);
        tCopy[sizeof(tCopy) - 1] = '\0';
        char* p1 = strchr(tCopy, '/');
        if (p1) {
            p1++;
            char* p2 = strchr(p1, '/');
            if (p2) {
                size_t idLen = (size_t)(p2 - p1);
                if (idLen < sizeof(devId)) {
                    memcpy(devId, p1, idLen);
                    devId[idLen] = '\0';
                }
            }
        }
    }

    const char* cmd    = doc["cmd"]       | "";
    const char* cmdId  = doc["cmdId"]     | "";
    bool requiresAck   = doc["requiresAck"] | false;
    uint8_t flags      = requiresAck ? FLAG_REQUIRES_ACK : 0;

    Serial.printf("[CMD] topic=%s cmd=%s dev=%s\n", topic, cmd, devId);

    // --- Gateway-Self-Commands (gateway-ID oder "gateway" segment) ---
    bool isGateway = (strcmp(devId, DEVICE_ID) == 0) ||
                     (strncmp(devId, "gateway", 7) == 0);
    if (isGateway) {
        if      (strcmp(cmd, "test_tx")     == 0) { testTxPending = true; }
        else if (strcmp(cmd, "reset")       == 0) { delay(300); ESP.restart(); }
        else if (strcmp(cmd, "GPS_TRIGGER") == 0) { startGatewayGps(); }
        else if (strcmp(cmd, "GPS_STOP")    == 0) { stopGatewayGps(); }
        else    { Serial.printf("[CMD] GW: unbekannt '%s'\n", cmd); }
        return;
    }

    if (devId[0] == '\0' || cmd[0] == '\0') return;

    // --- TTL prüfen ---
    uint32_t ttl = doc["ttl"] | (uint32_t)30;
    // Wenn timestamp im CMD vorhanden und NTP synchronisiert: TTL absolut prüfen
    // Sonst: TTL als Sekunden-Grenzwert für Queue-Alter (30s default)
    // (Vollständige absolute TTL-Prüfung erfordert NTP-Sync auf MQTT-Sender-Seite)

    // --- CMD → CMD_ID auflösen ---
    struct CmdEntry { const char* name; uint8_t id; };
    static const CmdEntry cmdTable[] = {
        { "SET_BEACON_INTERVAL",   CMD_SET_BEACON_INTERVAL   },
        { "SET_WAKE_INTERVAL",     CMD_SET_WAKE_INTERVAL     },
        { "SET_ACK_WINDOW",        CMD_SET_ACK_WINDOW        },
        { "SET_PROFILE",           CMD_SET_PROFILE           },
        { "SET_STATE",             CMD_SET_STATE             },
        { "TRIGGER_LOST",          CMD_TRIGGER_LOST          },
        { "TRIGGER_GPS",           CMD_TRIGGER_GPS           },
        { "TRIGGER_RECOVERY",      CMD_TRIGGER_RECOVERY      },
        { "TRIGGER_NORMAL",        CMD_TRIGGER_NORMAL        },
        { "FORCE_LORAWAN_JOIN",    CMD_FORCE_LORAWAN_JOIN    },
        { "SET_868_SF",            CMD_SET_868_SF            },
        { "SET_868_TX_POWER",      CMD_SET_868_TX_POWER      },
        { "SET_433_SF",            CMD_SET_433_SF            },
        { "ENABLE_MODULE",         CMD_ENABLE_MODULE         },
        { "DISABLE_MODULE",        CMD_DISABLE_MODULE        },
        { "BLE_SCAN",              CMD_BLE_SCAN              },
        { "SET_BLE_GEOFENCE",      CMD_SET_BLE_GEOFENCE      },
        { "CLEAR_BLE_GEOFENCE",    CMD_CLEAR_BLE_GEOFENCE    },
        { "GET_BLE_GEOFENCE",      CMD_GET_BLE_GEOFENCE      },
        { "WIFI_SCAN",             CMD_WIFI_SCAN             },
        { "GPS_TRIGGER",           CMD_GPS_TRIGGER           },
        { "GPS_STOP",              CMD_GPS_STOP              },
        { "GPS_CONTINUOUS",        CMD_GPS_CONTINUOUS        },
        { "GET_STATUS",            CMD_GET_STATUS            },
        { "GET_BATTERY",           CMD_GET_BATTERY           },
        { "GET_CONFIG",            CMD_GET_CONFIG            },
        { "GET_STATE",             CMD_GET_STATE             },
        { "GET_RSSI",              CMD_GET_RSSI              },
        { "PING",                  CMD_PING                  },
        { "REBOOT",                CMD_REBOOT                },
        { "FACTORY_RESET",         CMD_FACTORY_RESET         },
        { "SAVE_CONFIG",           CMD_SAVE_CONFIG           },
        { "SET_ACTIVE_TIME",       CMD_SET_ACTIVE_TIME       },
        { "SET_GW_TIMEOUT",        CMD_SET_GW_TIMEOUT        },
        { "SET_WARNING_TIMEOUT",   CMD_SET_WARNING_TIMEOUT   },
        { "SET_BLE_SCAN_DURATION", CMD_SET_BLE_SCAN_DURATION },
        { "TRIGGER_CHECK",         CMD_TRIGGER_CHECK         },
        { "SET_DEVICE_ID",         CMD_SET_DEVICE_ID         },
        { "SET_WIFI_CREDENTIALS",  CMD_SET_WIFI_CREDENTIALS  },
        { "LOAD_CONFIG",           CMD_LOAD_CONFIG           },
        { nullptr, 0 }
    };

    uint8_t cmdIdByte = 0;
    for (int i = 0; cmdTable[i].name; i++) {
        if (strcmp(cmd, cmdTable[i].name) == 0) { cmdIdByte = cmdTable[i].id; break; }
    }
    if (!cmdIdByte) {
        Serial.printf("[CMD] Unbekannter CMD: '%s'\n", cmd);
        return;
    }

    // --- Parameter kodieren ---
    JsonVariantConst p = doc["params"];
    uint8_t params[48] = {};
    uint8_t paramLen   = 0;

    switch (cmdIdByte) {
        case CMD_SET_BEACON_INTERVAL:
        case CMD_SET_WAKE_INTERVAL:
        case CMD_SET_ACK_WINDOW:
        case CMD_SET_GW_TIMEOUT:
        case CMD_SET_WARNING_TIMEOUT:
        case CMD_SET_BLE_SCAN_DURATION: {
            uint16_t v = p["value"] | (uint16_t)0;
            params[0] = (v >> 8) & 0xFF;
            params[1] =  v       & 0xFF;
            paramLen  = 2;
            break;
        }
        case CMD_SET_ACTIVE_TIME: {
            uint32_t v = p["value"] | (uint32_t)0;
            params[0] = (v >> 24) & 0xFF;
            params[1] = (v >> 16) & 0xFF;
            params[2] = (v >>  8) & 0xFF;
            params[3] =  v        & 0xFF;
            paramLen  = 4;
            break;
        }
        case CMD_SET_PROFILE: {
            params[0] = p["profile"] | (uint8_t)0;
            paramLen  = 1;
            break;
        }
        case CMD_SET_STATE: {
            params[0] = p["state"] | (uint8_t)0;
            paramLen  = 1;
            break;
        }
        case CMD_SET_868_SF:
        case CMD_SET_433_SF: {
            params[0] = p["sf"] | (uint8_t)7;
            paramLen  = 1;
            break;
        }
        case CMD_SET_868_TX_POWER: {
            params[0] = (uint8_t)((int8_t)(p["power"] | 14));
            paramLen  = 1;
            break;
        }
        case CMD_ENABLE_MODULE:
        case CMD_DISABLE_MODULE: {
            params[0] = p["module"] | (uint8_t)0;
            paramLen  = 1;
            break;
        }
        case CMD_SET_DEVICE_ID: {
            const char* newId = p["device_id"] | "";
            paramLen = (uint8_t)strnlen(newId, 15);
            memcpy(params, newId, paramLen);
            break;
        }
        case CMD_SET_BLE_GEOFENCE: {
            // Format: [mode 1B][slot 1B][name_len 1B][name bytes...]
            // JSON: { "mode": 0, "slot": 0, "name": "YANIS-PC" }
            const char* name = p["name"] | "";
            uint8_t mode     = p["mode"]  | (uint8_t)0;
            uint8_t slot     = p["slot"]  | (uint8_t)0;
            uint8_t nameLen  = (uint8_t)strnlen(name, 31);
            params[0] = mode;
            params[1] = slot;
            params[2] = nameLen;
            if (nameLen) memcpy(params + 3, name, nameLen);
            paramLen = 3 + nameLen;
            break;
        }
        case CMD_SET_WIFI_CREDENTIALS: {
            // Format: SSID\0PASS\0
            const char* ssid = p["ssid"] | "";
            const char* pass = p["pass"] | "";
            uint8_t sl = (uint8_t)strnlen(ssid, 31);
            uint8_t pl = (uint8_t)strnlen(pass, 31);
            memcpy(params, ssid, sl);
            params[sl] = '\0';
            memcpy(params + sl + 1, pass, pl);
            params[sl + 1 + pl] = '\0';
            paramLen = sl + 1 + pl + 1;
            break;
        }
        case CMD_REBOOT: {
            uint8_t del = p["delay_s"] | (uint8_t)3;
            params[0] = del;
            paramLen  = 1;
            break;
        }
        case CMD_FACTORY_RESET: {
            // Confirm-Byte 0xAA muss mitgesendet werden
            params[0] = 0xAA;
            paramLen  = 1;
            break;
        }
        case CMD_LOAD_CONFIG:
            paramLen = 0;   // keine Parameter
            break;
        default:
            paramLen = 0;
            break;
    }

    // --- Binary Frame bauen ---
    static uint16_t seqOut = 1;
    uint8_t  frame[64]  = {};
    uint8_t  frameLen   = (uint8_t)buildCmdFrame(frame, sizeof(frame),
                                                  cmdIdByte, seqOut++, flags,
                                                  params, paramLen);
    if (!frameLen) {
        Serial.println("[CMD] Frame-Build Fehler (zu gross?)");
        return;
    }

    if (enqueuePendingCmd(devId, cmdId, cmd, frame, frameLen, ttl)) {
        Serial.printf("[CMD] Eingereiht: %s → %s (TTL=%lus)\n",
                      cmd, devId, (unsigned long)ttl);
    } else {
        Serial.println("[CMD] Queue voll – CMD verworfen");
    }
}

// =============================================================
// MQTT VERBINDEN
// =============================================================
bool connectMQTT() {
    if (!wifiOK) return false;

    char willTopic[48];
    snprintf(willTopic, sizeof(willTopic), "dogytag/%s/status", DEVICE_ID);
    const char* willMsg = "{\"state\":\"offline\",\"device_id\":\"" DEVICE_ID "\"}";

    bool ok = mqtt.connect(CLIENT_ID, MQTT_USER, MQTT_PASS,
                           willTopic, 0, false, willMsg);
    if (ok) {
        mqttOK = true;
        // Gateway-eigene Commands
        char cmdTopic[48];
        snprintf(cmdTopic, sizeof(cmdTopic), "dogytag/%s/cmd", DEVICE_ID);
        mqtt.subscribe(cmdTopic);
        // Alle Tracker-Commands (Wildcard)
        mqtt.subscribe("dogytag/+/cmd");
        Serial.printf("[MQTT] Verbunden %s:%d  sub=%s + dogytag/+/cmd\n",
                      MQTT_HOST, MQTT_PORT, cmdTopic);
        flushMqttQueue();  // ← gepufferte Nachrichten sofort senden
        publishStatus();
    } else {
        mqttOK = false;
        Serial.printf("[MQTT] Fehler rc=%d (%s:%d)\n",
                      mqtt.state(), MQTT_HOST, MQTT_PORT);
    }
    return ok;
}

// =============================================================
// LORA TEST TX (Gateway-Beacon)
// =============================================================
void sendTestBeacon() {
    txCount++;
    uint8_t pkt[11] = {0};
    const char* id = GW_LORA_ID;
    for (int i = 0; i < 6 && id[i]; i++) pkt[i] = (uint8_t)id[i];
    pkt[6]  = (uint8_t)((txCount >> 8) & 0xFF);
    pkt[7]  = (uint8_t)(txCount & 0xFF);
    pkt[8]  = 0x00;
    pkt[9]  = 0x00;
    pkt[10] = 0xAB;

    int16_t rc = radio->transmit(pkt, sizeof(pkt));
    if (rc == RADIOLIB_ERR_NONE) {
        Serial.printf("[TX] Test-Beacon #%lu OK\n", (unsigned long)txCount);
        publishEvent("TEST_TX", "Test-Beacon gesendet", "info");
    } else {
        Serial.printf("[TX] Fehler: %d\n", rc);
    }
    radio->startReceive();
}

// =============================================================
// WIFI
// =============================================================
void setupWiFi() {
    Serial.printf("[WiFi] Verbinde '%s'...\n", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    WiFi.setAutoReconnect(true);

    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - t0) < 12000) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();

    wifiOK = (WiFi.status() == WL_CONNECTED);
    if (wifiOK) {
        Serial.printf("[WiFi] OK – %s\n", WiFi.localIP().toString().c_str());
        configTime(0, 0, "pool.ntp.org", "time.google.com");
        uint32_t nt0 = millis();
        while (time(nullptr) < 1000000000UL && (millis() - nt0) < 8000) delay(100);
        ntpSynced = (time(nullptr) > 1000000000UL);
        Serial.printf("[NTP] %s\n", ntpSynced ? "OK" : "Timeout");
    } else {
        Serial.println("[WiFi] Fehler – starte ohne WiFi");
    }
}

// =============================================================
// DISPLAY
// =============================================================
void drawSignalBars(uint16_t x, uint16_t y, int bars) {
    for (int i = 0; i < 5; i++) {
        uint16_t h  = (uint16_t)((i + 1) * 3 + 1);
        uint16_t bx = x + (uint16_t)(i * 7);
        uint16_t by = y + 16 - h;
        uint16_t col = (i < bars) ? ST7735_GREEN : ST7735_COLOR565(40, 40, 40);
        tft.st7735_fill_rectangle(bx, by, 5, h, col);
    }
}

void drawHeader(const char* title, uint8_t page) {
    uint16_t bg = ST7735_COLOR565(10, 10, 40);
    tft.st7735_fill_rectangle(0, 0, 160, 10, bg);
    tft.st7735_write_str(0, 0, title, Font_7x10, ST7735_CYAN, bg);
    tft.st7735_write_str(72,  0, "W:", Font_7x10, ST7735_COLOR565(130,130,130), bg);
    tft.st7735_write_str(86,  0, wifiOK ? "O" : "-",
                         Font_7x10, wifiOK ? ST7735_GREEN : ST7735_RED, bg);
    tft.st7735_write_str(93,  0, " M:", Font_7x10, ST7735_COLOR565(130,130,130), bg);
    tft.st7735_write_str(114, 0, mqttOK ? "O" : "-",
                         Font_7x10, mqttOK ? ST7735_GREEN : ST7735_RED, bg);
    char pi[6];
    snprintf(pi, sizeof(pi), "[%u/3]", page + 1);
    tft.st7735_write_str(128, 0, pi, Font_7x10, ST7735_COLOR565(80,80,80), bg);
    tft.st7735_fill_rectangle(0, 11, 160, 1, ST7735_COLOR565(40, 40, 80));
}

// Seite 0: Gateway-Status
void drawPage0() {
    char buf[32];
    tft.st7735_fill_rectangle(0, 12, 160, 68, ST7735_BLACK);
    drawHeader("GW01", 0);

    if (wifiOK) snprintf(buf, sizeof(buf), "IP: %s", WiFi.localIP().toString().c_str());
    else        snprintf(buf, sizeof(buf), "IP: -- WiFi fehlt");
    tft.st7735_write_str(0, 13, buf, Font_7x10, ST7735_WHITE, ST7735_BLACK);

    unsigned long upSec = (millis() - bootTime) / 1000;
    snprintf(buf, sizeof(buf), "Up: %02luh%02lum%02lus",
             upSec/3600, (upSec%3600)/60, upSec%60);
    tft.st7735_write_str(0, 23, buf, Font_7x10,
                         ST7735_COLOR565(160,160,160), ST7735_BLACK);

    snprintf(buf, sizeof(buf), "RX:%-4lu TX:%-4lu ACK:%-3lu",
             (unsigned long)rxCount, (unsigned long)txCount,
             (unsigned long)ackCount);
    tft.st7735_write_str(0, 33, buf, Font_7x10,
                         ST7735_COLOR565(120,120,120), ST7735_BLACK);

    if (mqttQCount > 0) {
        snprintf(buf, sizeof(buf), "MQTT:Q%u  WiFi:%ddBm",
                 mqttQCount, wifiOK ? (int)WiFi.RSSI() : 0);
        tft.st7735_write_str(0, 43, buf, Font_7x10, ST7735_YELLOW, ST7735_BLACK);
    } else if (mqttOK) {
        snprintf(buf, sizeof(buf), "MQTT:OK  WiFi:%ddBm", (int)WiFi.RSSI());
        tft.st7735_write_str(0, 43, buf, Font_7x10, ST7735_GREEN, ST7735_BLACK);
    } else {
        snprintf(buf, sizeof(buf), "MQTT:-- WiFi:%s", wifiOK ? "OK" : "--");
        tft.st7735_write_str(0, 43, buf, Font_7x10, ST7735_RED, ST7735_BLACK);
    }

    snprintf(buf, sizeof(buf), "LoRa %.1fMHz SF%d BW%.0f",
             LORA_FREQ_MHZ, LORA_SF, LORA_BW_KHZ);
    tft.st7735_write_str(0, 53, buf, Font_7x10,
                         ST7735_COLOR565(80,80,80), ST7735_BLACK);

    snprintf(buf, sizeof(buf), "Tags:%-2d  Err:%-4lu NTP:%s",
             activeTrackerCount(), (unsigned long)rxErrors,
             ntpSynced ? "OK" : "--");
    tft.st7735_write_str(0, 63, buf, Font_7x10,
                         ST7735_COLOR565(80,80,80), ST7735_BLACK);
}

// Seite 1: Letzter Tracker (Detail)
void drawPage1() {
    char buf[32];
    bool hasTracker = (lastTrackerIdx >= 0 && trackers[lastTrackerIdx].valid);

    if (!hasTracker) {
        tft.st7735_fill_screen(ST7735_BLACK);
        drawHeader("Tracker", 1);
        tft.st7735_write_str(4, 22, "Kein Tracker gesehen.",
                             Font_7x10, ST7735_COLOR565(120,120,120), ST7735_BLACK);
        snprintf(buf, sizeof(buf), "%.1fMHz SF%d sync=0x%02X",
                 LORA_FREQ_MHZ, LORA_SF, LORA_SYNC);
        tft.st7735_write_str(4, 34, buf, Font_7x10,
                             ST7735_COLOR565(60,60,60), ST7735_BLACK);
        tft.st7735_write_str(4, 46, "Warte auf Beacon...",
                             Font_7x10, ST7735_COLOR565(60,60,60), ST7735_BLACK);
        return;
    }

    const TrackerInfo& tr = trackers[lastTrackerIdx];
    tft.st7735_fill_rectangle(0, 12, 160, 68, ST7735_BLACK);
    drawHeader(tr.id, 1);

    snprintf(buf, sizeof(buf), "RSSI: %4ddBm", (int)tr.rssi);
    tft.st7735_write_str(0, 13, buf, Font_7x10, ST7735_WHITE, ST7735_BLACK);

    snprintf(buf, sizeof(buf), "SNR:  %+.1fdB", tr.snr);
    tft.st7735_write_str(0, 23, buf, Font_7x10,
                         ST7735_COLOR565(180,180,180), ST7735_BLACK);

    snprintf(buf, sizeof(buf), "Bat:  %.2fV   #%u",
             tr.batMv / 1000.0f, tr.counter);
    tft.st7735_write_str(0, 33, buf, Font_7x10, ST7735_YELLOW, ST7735_BLACK);

    tft.st7735_write_str(0, 45, rssiToQuality(tr.rssi), Font_7x10,
                         rssiToColor(tr.rssi), ST7735_BLACK);
    drawSignalBars(110, 43, rssiToBars(tr.rssi));

    unsigned long agoSec = (millis() - tr.lastSeen) / 1000;
    snprintf(buf, sizeof(buf), "Zuletzt: %lum%02lus ago",
             agoSec / 60, agoSec % 60);
    tft.st7735_write_str(0, 59, buf, Font_7x10,
                         ST7735_COLOR565(130,130,130), ST7735_BLACK);

    snprintf(buf, sizeof(buf), "ACK:%-4lu RX:%-4lu Err:%lu",
             (unsigned long)ackCount, (unsigned long)rxCount,
             (unsigned long)rxErrors);
    tft.st7735_write_str(0, 69, buf, Font_7x10,
                         ST7735_COLOR565(80,80,80), ST7735_BLACK);
}

// Seite 2: Alle Tracker
void drawPage2() {
    char buf[32];
    tft.st7735_fill_screen(ST7735_BLACK);

    char title[16];
    snprintf(title, sizeof(title), "Tags (%d)", activeTrackerCount());
    drawHeader(title, 2);

    int shown = 0;
    for (int i = 0; i < MAX_TRACKERS; i++) {
        if (!trackers[i].valid) continue;
        const TrackerInfo& tr = trackers[i];
        uint16_t y = (uint16_t)(13 + shown * 22);
        if (y > 68) break;

        snprintf(buf, sizeof(buf), "%-6s %4ddBm %.2fV",
                 tr.id, (int)tr.rssi, tr.batMv / 1000.0f);
        tft.st7735_write_str(0, y, buf, Font_7x10,
                             rssiToColor(tr.rssi), ST7735_BLACK);

        unsigned long agoSec = (millis() - tr.lastSeen) / 1000;
        snprintf(buf, sizeof(buf), " #%-4u %2lum%02lus %s",
                 tr.counter, agoSec/60, agoSec%60, rssiToQuality(tr.rssi));
        tft.st7735_write_str(0, y + 10, buf, Font_7x10,
                             ST7735_COLOR565(110,110,110), ST7735_BLACK);

        if (shown < activeTrackerCount() - 1 && y + 21 <= 69) {
            tft.st7735_fill_rectangle(0, y + 21, 160, 1,
                                      ST7735_COLOR565(35,35,35));
        }
        shown++;
    }

    if (shown == 0) {
        tft.st7735_write_str(4, 30, "Keine Tracker bekannt.",
                             Font_7x10, ST7735_COLOR565(80,80,80), ST7735_BLACK);
        tft.st7735_write_str(4, 42, "Warte auf Beacon...",
                             Font_7x10, ST7735_COLOR565(60,60,60), ST7735_BLACK);
    }
}

void drawDisplay() {
    switch (currentPage) {
        case 0: drawPage0(); break;
        case 1: drawPage1(); break;
        case 2: drawPage2(); break;
        default: currentPage = 0; drawPage0(); break;
    }
}

void drawBoot(const char* msg) {
    tft.st7735_fill_screen(ST7735_BLACK);
    tft.st7735_write_str(2, 2,  "SmartTag GW",    Font_11x18, ST7735_CYAN,   ST7735_BLACK);
    tft.st7735_write_str(2, 24, FIRMWARE_VERSION, Font_7x10,  ST7735_WHITE,  ST7735_BLACK);
    tft.st7735_write_str(2, 38, msg,              Font_7x10,  ST7735_YELLOW, ST7735_BLACK);
}

// =============================================================
// 433MHz MODEM – Implementierungen
// (nach parsePacket / findOrAddTracker / queuedPublish)
// =============================================================
void send433(const uint8_t* data, size_t len) {
    char hexStr[129];
    bytesToHex433(data, len, hexStr);
    serial433.printf("TX:%s\n", hexStr);
    tx433Count++;
    Serial.printf("[433] TX %u Bytes\n", (unsigned)len);
}

void handle433RxLine(const char* line) {
    const char* p = line + 3;
    int rssi = atoi(p);
    const char* p2 = strchr(p, ':');
    if (!p2) return;
    int snr = atoi(p2 + 1);
    const char* p3 = strchr(p2 + 1, ':');
    if (!p3) return;
    const char* hexStr = p3 + 1;

    uint8_t buf[64];
    int len = hexToBytes433(hexStr, buf, sizeof(buf));
    if (len <= 0) return;

    rx433Count++;
    Serial.printf("[433] RX RSSI=%d SNR=%d len=%d\n", rssi, snr, len);

    TrackerInfo parsed;
    if (parsePacket(buf, (size_t)len, &parsed)) {
        int idx = findOrAddTracker(parsed.id);
        trackers[idx].batMv    = parsed.batMv;
        trackers[idx].counter  = parsed.counter;
        trackers[idx].rssi     = (int16_t)rssi;
        trackers[idx].snr      = (float)snr;
        trackers[idx].lastSeen = millis();
        trackers[idx].valid    = true;
        lastTrackerIdx         = idx;

        char topic[64];
        snprintf(topic, sizeof(topic), "dogytag/%s/lora433", parsed.id);

        StaticJsonDocument<256> doc;
        doc["device_id"]  = parsed.id;
        doc["timestamp"]  = getTimestamp();
        doc["rssi"]       = rssi;
        doc["snr"]        = snr;
        doc["gateway"]    = DEVICE_ID;
        doc["counter"]    = parsed.counter;
        doc["battery_mv"] = parsed.batMv;
        doc["band"]       = "433";

        char jbuf[256];
        serializeJson(doc, jbuf, sizeof(jbuf));
        queuedPublish(topic, jbuf);
        Serial.printf("[433] Tracker %s RSSI=%d bat=%d\n",
                      parsed.id, rssi, parsed.batMv);
    }
}

void poll433() {
    while (serial433.available()) {
        char c = (char)serial433.read();
        if (c == '\n' || c == '\r') {
            if (m433CmdPos > 0) {
                m433CmdBuf[m433CmdPos] = '\0';
                Serial.printf("[433] <- %s\n", m433CmdBuf);
                if (strcmp(m433CmdBuf, "READY") == 0 ||
                    strncmp(m433CmdBuf, "STATUS:", 7) == 0) {
                    bool wasReady = m433Ready;
                    m433Ready = true;
                    if (strncmp(m433CmdBuf, "STATUS:", 7) == 0) {
                        strncpy(m433StatusBuf, m433CmdBuf, sizeof(m433StatusBuf) - 1);
                        m433StatusBuf[sizeof(m433StatusBuf) - 1] = '\0';
                    }
                    serial433.println("OK");  // hostConnected im CubeCell setzen
                    Serial.printf("[433] Modem bereit: %s\n", m433CmdBuf);
                    // Bei erstem Verbindungsaufbau: Auto-Beacon aktivieren (10s Intervall)
                    if (!wasReady) {
                        serial433.println("BCAST:10");
                        Serial.println("[433] Auto-Beacon aktiviert (10s)");
                    }
                } else if (strncmp(m433CmdBuf, "RX:", 3) == 0) {
                    handle433RxLine(m433CmdBuf);
                }
                m433CmdPos = 0;
            }
        } else if (m433CmdPos < sizeof(m433CmdBuf) - 1) {
            m433CmdBuf[m433CmdPos++] = c;
        }
    }
}

// =============================================================
// SETUP
// =============================================================
void setup() {
    bootTime = millis();

    Serial.begin(115200);
    Serial.setTxTimeoutMs(0);   // HWCDC: nie blockieren wenn kein Host
    uint32_t t0 = millis();
    while (!Serial && (millis() - t0) < 8000) delay(10);  // max 8s auf Monitor warten
    delay(50);
    Serial.println("[BOOT] " FIRMWARE_VERSION);
    Serial.println("[BOOT] SmartTag Gateway – Bidirektional LoRa+MQTT EU868");
    Serial.printf("[BOOT] MQTT: %s:%d user=%s\n", MQTT_HOST, MQTT_PORT, MQTT_USER);
    Serial.flush();

    pinMode(BTN_TX_TEST, INPUT_PULLUP);

    memset(trackers,  0, sizeof(trackers));
    memset(mqttQueue, 0, sizeof(mqttQueue));

    // 433MHz Modem (CubeCell) initialisieren
    serial433.begin(MODEM433_BAUD, SERIAL_8N1, MODEM433_RX_PIN, MODEM433_TX_PIN);
    Serial.printf("[433] UART2 bereit: RX=GPIO%d TX=GPIO%d\n",
                  MODEM433_RX_PIN, MODEM433_TX_PIN);

    // Wireless Tracker v1.2: GPIO3 = VGNSS_CTRL (active-LOW) powers TFT + GPS
    pinMode(3, OUTPUT);
    digitalWrite(3, LOW);
    delay(200);
    Serial.println("[GW-GPS] UC6580 POWER ON (GPIO3 LOW) – auf Abruf via MQTT GPS_TRIGGER");

    tft.st7735_init();
    tft.st7735_fill_screen(ST7735_BLACK);
    drawBoot("Init...");
    delay(300);
    Serial.println("[TFT] OK");
    Serial.flush();

    drawBoot("Verbinde WiFi...");
    setupWiFi();

    mqtt.setServer(MQTT_HOST, MQTT_PORT);
    mqtt.setCallback(mqttCallback);
    mqtt.setKeepAlive(60);
    mqtt.setSocketTimeout(10);
    mqtt.setBufferSize(1024);

    if (wifiOK) {
        drawBoot("Verbinde MQTT...");
        connectMQTT();
    }

    drawBoot("Init LoRa...");
    loraSPI.begin(9, 11, 10, LORA_NSS);
    radio = new SX1262(new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY, loraSPI));

    // Vorab-Check: SX1262 BUSY nach Reset LOW? (RadioLib hat keinen eigenen Timeout)
    pinMode(LORA_RST,  OUTPUT); digitalWrite(LORA_RST, LOW);  delay(20);
    pinMode(LORA_BUSY, INPUT);  digitalWrite(LORA_RST, HIGH); delay(20);
    {
        uint32_t busyT = millis();
        while (digitalRead(LORA_BUSY) && (millis() - busyT) < 3000) delay(5);
        if (digitalRead(LORA_BUSY)) {
            Serial.println("[ERROR] SX1262 BUSY haengt – kein Power?");
            Serial.flush();
            drawBoot("SX1262 kein Power?");
            delay(3000);
            ESP.restart();
        }
    }

    int16_t rc = radio->begin();
    if (rc != RADIOLIB_ERR_NONE) {
        Serial.printf("[ERROR] SX1262 init: %d\n", rc);
        Serial.flush();
        drawBoot("SX1262 Fehler!");
        delay(3000);
        ESP.restart();
    }

    radio->setFrequency(LORA_FREQ_MHZ);
    radio->setBandwidth(LORA_BW_KHZ);
    radio->setSpreadingFactor(LORA_SF);
    radio->setCodingRate(LORA_CR);
    radio->setSyncWord(LORA_SYNC);
    radio->setOutputPower(LORA_TX_PWR);
    radio->setRxBoostedGainMode(true);
    radio->setPacketReceivedAction(onLoraPacket);

    Serial.printf("[LORA] Bereit: %.1fMHz SF%d BW%.0fkHz sync=0x%02X Bidir=ON\n",
                  LORA_FREQ_MHZ, LORA_SF, LORA_BW_KHZ, LORA_SYNC);
    Serial.flush();

    radioReady = true;
    startRx();

    drawBoot("Bereit – empfange...");
    delay(400);

    tft.st7735_fill_screen(ST7735_BLACK);
    drawDisplay();

    Serial.println("[BOOT] OK");
}

// =============================================================
// LOOP
// =============================================================
void loop() {
    uint32_t now = millis();

    // --- Gateway GPS pollen (wenn aktiv) ---
    pollGatewayGps();

    // --- 433MHz Modem pollen ---
    poll433();

    // --- Ping Modem: alle 5s bis bereit, dann alle 60s zur Zustandsprüfung ---
    {
        uint32_t pingInterval = m433Ready ? 60000UL : 5000UL;
        if (now - last433PingMs > pingInterval) {
            last433PingMs = now;
            serial433.println("STATUS");
            if (!m433Ready) Serial.println("[433] Ping Modem...");
        }
    }

    // --- WiFi Reconnect ---
    wifiOK = (WiFi.status() == WL_CONNECTED);
    if (!wifiOK && (now - lastWifiRetry > WIFI_RETRY_MS)) {
        lastWifiRetry = now;
        Serial.println("[WiFi] Reconnect...");
        WiFi.reconnect();
    }

    // --- MQTT Loop + Reconnect ---
    if (wifiOK) {
        if (mqtt.connected()) {
            mqttOK = true;
            mqtt.loop();
        } else {
            mqttOK = false;
            if (now - lastMqttRetry > MQTT_RETRY_MS) {
                lastMqttRetry = now;
                Serial.println("[MQTT] Reconnect...");
                connectMQTT();
            }
        }
    } else {
        mqttOK = false;
    }

    // --- LoRa RX ---
    if (radioReady && loraPacketReady) {
        loraPacketReady = false;
        uint8_t buf[64] = {0};
        size_t  len     = radio->getPacketLength();
        // RSSI/SNR vor readData lesen – SX1262 Register nur im RX-Done-State gültig
        int16_t rxRssi  = (int16_t)radio->getRSSI();
        float   rxSnr   = radio->getSNR();
        if (len > sizeof(buf)) len = sizeof(buf);

        int16_t rc = radio->readData(buf, len);

        if (rc == RADIOLIB_ERR_NONE) {
            rxCount++;

            // RESP-Paket vom Tag? (Antwort auf einen CMD)
            if (len >= 11 && buf[0] == PKT_RESP) {
                handleRespPacket(buf, len, rxRssi, rxSnr);
            // GPS-Paket vom Tag?
            } else if (len >= 20 && buf[0] == PKT_GPS) {
                handleGpsPacket(buf, len, rxRssi, rxSnr);
            // LOG-Paket vom Tag?
            } else if (len >= 12 && buf[0] == PKT_LOG_GW) {
                handleLogPacket(buf, len, rxRssi, rxSnr);
            // Beacon parsen
            } else {
                TrackerInfo parsed;
                if (parsePacket(buf, len, &parsed)) {
                    int idx = findOrAddTracker(parsed.id);
                    trackers[idx].rssi     = rxRssi;
                    trackers[idx].snr      = rxSnr;
                    trackers[idx].batMv    = parsed.batMv;
                    trackers[idx].counter  = parsed.counter;
                    trackers[idx].lastSeen = now;
                    trackers[idx].valid    = true;
                    lastTrackerIdx         = idx;

                    Serial.printf("[RX] #%u %s RSSI:%d SNR:%.1f Bat:%dmV\n",
                                  parsed.counter, parsed.id, rxRssi, rxSnr, parsed.batMv);

                    publishLoraPacket(trackers[idx]);
                    drawDisplay();

                    // Beacon auf 433MHz weiterleiten (Mesh-Relay)
                    if (m433Ready) send433(buf, len);

                    // ACK zurücksenden (+ CMD aus Queue falls vorhanden)
                    sendAck(parsed.id, rxRssi, rxSnr);

                } else {
                    Serial.printf("[RX] Unbekanntes Paket len=%u RSSI:%d\n",
                                  (unsigned)len, (int)rxRssi);
                }
            } // end RESP/LOG/Beacon dispatch
        } else {
            rxErrors++;
            Serial.printf("[RX] Fehler rc=%d\n", (int)rc);
        }

        startRx();
    }

    // --- Button ---
    bool btnNow = digitalRead(BTN_TX_TEST);

    if (btnNow == LOW && lastBtnState == HIGH) {
        btnPressedAt = now;
        btnLongFired = false;
    }
    if (btnNow == LOW && !btnLongFired && (now - btnPressedAt > BTN_LONG_MS)) {
        btnLongFired  = true;
        testTxPending = true;
        Serial.println("[BTN] Langer Druck – Test-TX");
    }
    if (btnNow == HIGH && lastBtnState == LOW) {
        if (!btnLongFired && (now - btnPressedAt > BTN_DEBOUNCE_MS)) {
            currentPage = (currentPage + 1) % TOTAL_PAGES;
            lastPageChange = now;
            Serial.printf("[BTN] Seite %u/%u\n", currentPage + 1, TOTAL_PAGES);
            tft.st7735_fill_screen(ST7735_BLACK);
            drawDisplay();
        }
    }
    lastBtnState = btnNow;

    // --- Test-TX ---
    if (testTxPending) {
        testTxPending = false;
        sendTestBeacon();
        drawDisplay();
    }

    // --- Auto-Cycle ---
    if (now - lastPageChange > PAGE_CYCLE_MS) {
        lastPageChange = now;
        currentPage = (currentPage + 1) % TOTAL_PAGES;
        tft.st7735_fill_screen(ST7735_BLACK);
        drawDisplay();
    }

    // --- Tracker + Command-Queue aufräumen ---
    if (now % 5000 < 20) {
        expireTrackers();
        expirePendingCmds();
    }

    // --- Periodische MQTT-Publikationen ---
    if (now - lastStatusTs > STATUS_INTERVAL_MS) {
        lastStatusTs = now;
        publishStatus();
    }
    if (now - lastTelemetryTs > TELEMETRY_INTERVAL_MS) {
        lastTelemetryTs = now;
        publishTelemetry();
    }

    // --- Display-Update ---
    if (now - lastDisplayTs > DISPLAY_INTERVAL_MS) {
        lastDisplayTs = now;
        drawDisplay();
    }

    delay(10);
}
