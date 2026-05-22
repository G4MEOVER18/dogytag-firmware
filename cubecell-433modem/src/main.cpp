// =============================================================
// SmartTag CubeCell HTCC-AB02A – 433MHz UART Modem v1.0.0
// Chip: ASR6502 (ARM Cortex-M0+ + SX1262)
// Frequenz: 433.775 MHz | SF7 | BW125 | CR4/5 | Sync=0xAB
//
// Verbindung zum Heltec (Board: HTCC-AB02A = cubecell_node, ASR6502):
//   Serial1 mit Pin-Remap auf GPIO4 (RX ← Heltec TX) / GPIO5 (TX → Heltec RX)
//   GPIO4 = P6_4, GPIO5 = P3_4 (physische Board-Header Pins)
//
// Protokoll (ASCII, '\n'-terminiert):
//   Heltec → Modem:
//     TX:<hexstring>\n       Paket senden
//     SF:<7-12>\n            Spreading Factor setzen
//     STATUS\n               Status abfragen
//
//   Modem → Heltec:
//     READY\n                Boot OK
//     OK\n                   Befehl ausgeführt
//     ERR:<msg>\n            Fehler
//     RX:<rssi>:<snr>:<hexstring>\n  Empfangenes Paket
//     STATUS:<info>\n        Statusantwort
// =============================================================

#include "LoRaWan_APP.h"
#include "Arduino.h"

// =============================================================
// KONFIGURATION
// =============================================================
#define FIRMWARE_VERSION     "v3.0.0-modem"
#define RF_FREQUENCY         433775000   // 433.775 MHz
#define TX_OUTPUT_POWER      14          // dBm
#define LORA_BANDWIDTH       0           // 0=125kHz, 1=250kHz, 2=500kHz
#define LORA_SF_DEFAULT      7           // SF7..SF12
#define LORA_CODINGRATE      1           // 1=4/5, 2=4/6, 3=4/7, 4=4/8
#define LORA_PREAMBLE_LEN    8
#define LORA_SYNC_WORD       0xAB        // SmartTag privates Sync-Word

#define UART_BAUD            115200
#define CMD_BUF_SIZE         160         // Max. Zeilenlänge

// =============================================================
// RADIO CALLBACKS
// =============================================================
static void OnTxDone(void);
static void OnTxTimeout(void);
static void OnRxDone(uint8_t* payload, uint16_t size, int16_t rssi, int8_t snr);
static void OnRxTimeout(void);
static void OnRxError(void);

static RadioEvents_t RadioEvents;

// =============================================================
// STATE
// =============================================================
static uint8_t  currentSF      = LORA_SF_DEFAULT;
static bool     txDone         = false;
static bool     txTimedOut     = false;
static uint32_t rxCount        = 0;
static uint32_t txCount        = 0;
static uint32_t errCount       = 0;
static bool     hostConnected  = false;   // Host hat reagiert
static uint32_t lastReadyMs    = 0;       // letztes READY gesendet

// Auto-Beacon (BCAST)
static char     g_deviceId[8]      = "CC433A";  // Gerätename für Beacon-Pakete
static uint32_t g_bcastIntervalMs  = 0;         // 0 = deaktiviert
static uint32_t g_lastBcastMs      = 0;
static uint16_t g_bcastCounter     = 0;

// Periodischer STATUS-Bericht an Host
static uint32_t g_lastStatusMs     = 0;

// --- Empfangspuffer ---
static uint8_t  rxBuf[64];
static uint16_t rxLen  = 0;
static int16_t  rxRssi = 0;
static int8_t   rxSnr  = 0;
static bool     rxReady = false;

// --- Kommandopuffer (Serial1) ---
static char     cmdBuf[CMD_BUF_SIZE];
static uint8_t  cmdPos = 0;

// =============================================================
// HILFSFUNKTIONEN
// =============================================================
static void bytesToHex(const uint8_t* data, uint16_t len, char* out) {
    for (uint16_t i = 0; i < len; i++) {
        sprintf(out + i * 2, "%02X", data[i]);
    }
    out[len * 2] = '\0';
}

static int hexToBytes(const char* hex, uint8_t* out, size_t maxLen) {
    size_t hexLen = strlen(hex);
    if (hexLen == 0 || hexLen % 2 != 0) return -1;
    size_t byteLen = hexLen / 2;
    if (byteLen > maxLen) return -1;
    for (size_t i = 0; i < byteLen; i++) {
        char byteStr[3] = { hex[i * 2], hex[i * 2 + 1], '\0' };
        char* endPtr;
        out[i] = (uint8_t)strtol(byteStr, &endPtr, 16);
        if (*endPtr != '\0') return -1;
    }
    return (int)byteLen;
}

static void configureTx();  // Vorwärtsdeklaration

// =============================================================
// BEACON SENDEN (11-Byte SmartTag-Format: [ID 6B][CNT 2B][BAT 2B][0xAB])
// =============================================================
static void sendBeacon() {
    uint8_t pkt[11];
    size_t idLen = strlen(g_deviceId);
    for (int i = 0; i < 6; i++) pkt[i] = (i < (int)idLen) ? (uint8_t)g_deviceId[i] : 0;
    g_bcastCounter++;
    pkt[6]  = (g_bcastCounter >> 8) & 0xFF;
    pkt[7]  =  g_bcastCounter       & 0xFF;
    pkt[8]  = 0x00;  // Batterie hi (nicht verfügbar)
    pkt[9]  = 0x00;  // Batterie lo
    pkt[10] = 0xAB;  // Marker
    configureTx();
    Radio.Send(pkt, sizeof(pkt));
    txCount++;
    Serial.printf("[BCAST] #%u ID=%s SF%d\n",
                  (unsigned)g_bcastCounter, g_deviceId, currentSF);
}

// =============================================================
// RADIO KONFIGURATION
// =============================================================
static void configureRx() {
    Radio.SetRxConfig(
        MODEM_LORA,
        LORA_BANDWIDTH,
        currentSF,
        LORA_CODINGRATE,
        0,                  // AFC (FSK unused)
        LORA_PREAMBLE_LEN,
        0,                  // Symbol-Timeout
        false,              // fixLen
        0,                  // payloadLen
        true,               // CRC ein
        0,                  // freqHopOn
        0,                  // hopPeriod
        false,              // IQ invertiert
        true                // rxContinuous
    );
}

static void startRx() {
    configureRx();
    Radio.Rx(0);  // continuous
}

static void configureTx() {
    Radio.SetTxConfig(
        MODEM_LORA,
        TX_OUTPUT_POWER,
        0,                  // fsk_dev (unused)
        LORA_BANDWIDTH,
        currentSF,
        LORA_CODINGRATE,
        LORA_PREAMBLE_LEN,
        false,              // fixLen
        true,               // CRC ein
        0,                  // freqHopOn
        0,                  // hopPeriod
        false,              // IQ invertiert
        3000                // Timeout ms
    );
}

// =============================================================
// KOMMANDO-VERARBEITUNG
// =============================================================
static void processCommand(const char* line) {
    hostConnected = true;  // jeder Befehl = Host lebt

    if (strncmp(line, "TX:", 3) == 0) {
        // TX:<hexstring> – Paket senden
        uint8_t pkt[64];
        int len = hexToBytes(line + 3, pkt, sizeof(pkt));
        if (len <= 0) {
            Serial.println("ERR:bad_hex");
            Serial.printf("[CMD] TX: Hex-Fehler: %s\n", line + 3);
            return;
        }
        configureTx();
        Radio.Send(pkt, (uint8_t)len);
        txCount++;
        // Antwort kommt in OnTxDone / OnTxTimeout
        Serial.printf("[TX] %d Bytes @ SF%d 433MHz\n", len, currentSF);

    } else if (strncmp(line, "SF:", 3) == 0) {
        // SF:<7-12> – Spreading Factor ändern
        int sf = atoi(line + 3);
        if (sf < 7 || sf > 12) {
            Serial.println("ERR:sf_range_7-12");
            return;
        }
        currentSF = (uint8_t)sf;
        startRx();
        Serial.println("OK");
        Serial.printf("[CFG] SF=%d\n", currentSF);

    } else if (strcmp(line, "STATUS") == 0) {
        char buf[128];
        snprintf(buf, sizeof(buf),
                 "STATUS:%s 433MHz SF%d RX:%lu TX:%lu ERR:%lu BCAST:%lus ID:%s",
                 FIRMWARE_VERSION, currentSF,
                 (unsigned long)rxCount,
                 (unsigned long)txCount,
                 (unsigned long)errCount,
                 (unsigned long)(g_bcastIntervalMs / 1000),
                 g_deviceId);
        Serial.println(buf);
        Serial.println(buf);

    } else if (strncmp(line, "BCAST:", 6) == 0) {
        // BCAST:<secs> – Auto-Beacon-Intervall setzen (0 = aus)
        int secs = atoi(line + 6);
        if (secs < 0) secs = 0;
        g_bcastIntervalMs = (uint32_t)secs * 1000UL;
        g_lastBcastMs = millis();  // sofort beim nächsten Intervall starten
        Serial.println("OK");
        if (secs > 0) {
            Serial.printf("[BCAST] Auto-Beacon alle %ds aktiviert\n", secs);
        } else {
            Serial.println("[BCAST] Auto-Beacon deaktiviert");
        }

    } else if (strncmp(line, "ID:", 3) == 0) {
        // ID:<str> – Geräte-ID setzen (max 6 Zeichen, für Beacon-Pakete)
        const char* newId = line + 3;
        size_t len = strlen(newId);
        if (len == 0 || len > 6) {
            Serial.println("ERR:id_max_6_chars");
            return;
        }
        strncpy(g_deviceId, newId, sizeof(g_deviceId) - 1);
        g_deviceId[sizeof(g_deviceId) - 1] = '\0';
        Serial.println("OK");
        Serial.printf("[CFG] ID=%s\n", g_deviceId);

    } else {
        Serial.println("ERR:unknown_cmd");
        Serial.printf("[CMD] Unbekannt: %s\n", line);
    }
}

// =============================================================
// SETUP
// =============================================================
void setup() {
    Serial.begin(UART_BAUD);
    delay(300);
    Serial.println("[BOOT] " FIRMWARE_VERSION " – SmartTag 433MHz Modem");
    Serial.println("[BOOT] UART=Serial(P3_0/RX P3_1/TX) shared debug+host");

    // Serial (UART0, P3_0=RX / P3_1=TX) wird für Host-Kommunikation UND USB-Debug genutzt.
    // UART2 (Serial1) hat keine externen Pins im PSoC4-Design der HTCC-AB02A → unbrauchbar.
    // Verdrahtung:
    //   Heltec GPIO5 (TX2) → CubeCell Board-Pin "RX" (P3_0)
    //   Heltec GPIO4 (RX2) ← CubeCell Board-Pin "TX" (P3_1)
    // Serial.begin() wird im CubeCell-Framework vor setup() aufgerufen – kein erneuter Aufruf nötig.

    // Radio initialisieren
    RadioEvents.TxDone    = OnTxDone;
    RadioEvents.TxTimeout = OnTxTimeout;
    RadioEvents.RxDone    = OnRxDone;
    RadioEvents.RxTimeout = OnRxTimeout;
    RadioEvents.RxError   = OnRxError;

    Radio.Init(&RadioEvents);
    Radio.SetPublicNetwork(false);
    Radio.SetChannel(RF_FREQUENCY);
    Radio.SetSyncWord(LORA_SYNC_WORD);

    startRx();

    Serial.printf("[LORA] Bereit: %uHz SF%d BW125kHz sync=0x%02X\n",
                  RF_FREQUENCY, currentSF, LORA_SYNC_WORD);

    Serial.println("READY");
    Serial.println("[BOOT] OK");
}

// =============================================================
// LOOP
// =============================================================
void loop() {
    Radio.IrqProcess();

    // --- READY periodisch senden bis Host antwortet (alle 4s) ---
    if (!hostConnected && (millis() - lastReadyMs > 4000)) {
        lastReadyMs = millis();
        Serial.println("READY");
        Serial.println("[KEEP] READY gesendet");
    }

    // --- Periodischer STATUS-Bericht an Host (alle 60s wenn verbunden) ---
    if (hostConnected && (millis() - g_lastStatusMs > 60000)) {
        g_lastStatusMs = millis();
        char buf[128];
        snprintf(buf, sizeof(buf),
                 "STATUS:%s 433MHz SF%d RX:%lu TX:%lu ERR:%lu BCAST:%lus ID:%s",
                 FIRMWARE_VERSION, currentSF,
                 (unsigned long)rxCount,
                 (unsigned long)txCount,
                 (unsigned long)errCount,
                 (unsigned long)(g_bcastIntervalMs / 1000),
                 g_deviceId);
        Serial.println(buf);
        Serial.println(buf);
    }

    // --- Auto-Beacon (BCAST) ---
    if (g_bcastIntervalMs > 0 && (millis() - g_lastBcastMs >= g_bcastIntervalMs)) {
        g_lastBcastMs = millis();
        sendBeacon();
    }

    // --- TX abgeschlossen ---
    if (txDone) {
        txDone = false;
        if (txTimedOut) {
            Serial.println("ERR:tx_timeout");
            Serial.println("[TX] Timeout");
            txTimedOut = false;
            errCount++;
        } else {
            Serial.println("OK");
        }
        startRx();
    }

    // --- Paket empfangen ---
    if (rxReady) {
        rxReady = false;

        char hexStr[129];  // max 64 Bytes = 128 Hex-Zeichen
        bytesToHex(rxBuf, rxLen, hexStr);

        char out[160];
        snprintf(out, sizeof(out), "RX:%d:%d:%s",
                 (int)rxRssi, (int)rxSnr, hexStr);
        Serial.println(out);
        Serial.println(out);  // auch auf USB-Debug ausgeben
    }

    // --- Serial1: Zeilen vom Heltec lesen ---
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            if (cmdPos > 0) {
                cmdBuf[cmdPos] = '\0';
                processCommand(cmdBuf);
                cmdPos = 0;
            }
        } else if (cmdPos < CMD_BUF_SIZE - 1) {
            cmdBuf[cmdPos++] = c;
        }
    }
}

// =============================================================
// RADIO CALLBACKS
// =============================================================
static void OnTxDone(void) {
    txDone = true;
}

static void OnTxTimeout(void) {
    txDone     = true;
    txTimedOut = true;
}

static void OnRxDone(uint8_t* payload, uint16_t size, int16_t rssi, int8_t snr) {
    uint16_t n = (size < sizeof(rxBuf)) ? size : sizeof(rxBuf);
    memcpy(rxBuf, payload, n);
    rxLen   = n;
    rxRssi  = rssi;
    rxSnr   = snr;
    rxReady = true;
    rxCount++;
}

static void OnRxTimeout(void) {
    startRx();
}

static void OnRxError(void) {
    errCount++;
    Serial.println("[RX] Fehler");
    startRx();
}
