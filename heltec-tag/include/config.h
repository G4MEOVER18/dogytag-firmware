#pragma once
#include <stdint.h>
#include <string.h>
// =============================================================
// DogyTag Tracker — Konfigurationsstruktur v1
// Testphase: POWER MANAGEMENT VORBEREITET, NICHT AKTIV
// =============================================================

enum class TrackerState : uint8_t {
    DEEP_SLEEP = 0,
    NORMAL     = 1,
    CHECK      = 2,
    WARNING    = 3,
    LOST       = 4,
    PRECISION  = 5,
    RECOVERY   = 6
};

enum class PowerProfile : uint8_t {
    TEST       = 0,
    PRODUCTION = 1
};

inline const char* stateName(TrackerState s) {
    switch(s) {
        case TrackerState::DEEP_SLEEP: return "DEEP_SLEEP";
        case TrackerState::NORMAL:     return "NORMAL";
        case TrackerState::CHECK:      return "CHECK";
        case TrackerState::WARNING:    return "WARNING";
        case TrackerState::LOST:       return "LOST";
        case TrackerState::PRECISION:  return "PRECISION";
        case TrackerState::RECOVERY:   return "RECOVERY";
        default:                       return "UNKNOWN";
    }
}

struct TrackerConfig {
    // --- Identity ---
    char     deviceId[16]           = "ST-002";
    char     firmwareVersion[16]    = "v1.5.6";
    uint16_t configVersion          = 3;

    // --- Profile ---
    PowerProfile profile            = PowerProfile::TEST;

    // --- Timing TEST defaults (PROD values in comments) ---
    uint16_t beaconInterval_s       = 5;      // PROD: 60
    uint16_t wakeInterval_s         = 60;     // PROD: 300
    uint16_t ackWindow_ms           = 600;    // PROD: 600
    uint16_t cmdWindow_ms           = 400;    // Extra RX nach ACK auf CMD warten
    uint16_t bleScanDuration_ms     = 3000;   // PROD: 2000
    uint16_t wifiScanDuration_ms    = 5000;   // PROD: 3000
    uint32_t activeTime_ms          = 30000;  // PROD: 10000
    uint16_t sleepTime_s            = 60;     // PROD: 300
    uint16_t gpsTimeout_s           = 120;
    uint16_t gwTimeout_s            = 60;
    uint16_t warningTimeout_s       = 120;

    // --- State Machine Thresholds ---
    // NORMAL→CHECK: jede wakeInterval_s
    // CHECK→WARNING: bleGeoFail && wifiGeoFail
    // WARNING→LOST: lastGwSeenAge > warningTimeout_s (als ms: * 1000)
    // LOST→PRECISION: GPS-Fix OK
    // PRECISION→RECOVERY: gwInRange wieder true
    // RECOVERY→NORMAL: manuell oder nach 60s
    uint8_t  checkFailsForWarning   = 2;    // X aufeinanderfolgende CHECK-Fails
    uint8_t  warningFailsForLost    = 3;    // X WARNING-Zyklen ohne GW

    // --- LoRa 868 ---
    float    freq868_mhz            = 868.0f;
    uint8_t  sf868                  = 7;
    float    bw868_khz              = 125.0f;
    uint8_t  cr868                  = 5;
    int8_t   txPower868             = 14;
    uint8_t  sync868                = 0xAB;

    // --- LoRa 433 ---
    float    freq433_mhz            = 433.775f;
    uint8_t  sf433                  = 7;
    int8_t   txPower433             = 14;

    // --- WiFi ---
    char     wifiSsid[64]           = "YourDataMyData";
    char     wifiPass[64]           = "";
    bool     wifiGeofenceEnabled    = false;
    char     wifiGeofenceSsid[64]   = "";

    // --- BLE ---
    uint8_t  bleGeofenceMacs[3][6]  = {};
    char     bleGeofenceNames[3][32]= {"YANIS-PC", "", ""}; // Default: YANIS-PC
    uint8_t  bleGeofenceCount       = 1;     // 1 Gerät vorkonfiguriert
    uint8_t  bleGeofenceMode        = 0;     // 0=ANY, 1=ALL
    bool     bleGeofenceEnabled     = false; // aktiviert via applyTestProfile()

    // --- Module Enable Flags ---
    bool     bleEnabled             = false; // aktiviert via applyTestProfile()
    bool     wifiEnabled            = false;  // TODO: WiFi scan im CHECK
    bool     gpsEnabled             = true;   // NEO-M9N: GPIO19(RX) GPIO20(TX)
    bool     lora868Enabled         = true;
    bool     lora433Enabled         = true;
    bool     lorawanEnabled         = false;

    // --- Power Management (VORBEREITET, NICHT AKTIV) ---
    bool     deepSleepEnabled       = false;
    bool     lightSleepEnabled      = false;

    // --- State-adaptive Beacon-Intervalle ---
    // Im LOST/PRECISION State kürzere Intervalle für schnellere Ortbarkeit
    uint16_t beaconIntervalLost_s   = 15;    // PROD: 30

    // --- Evidence Score Schwellen (mehrstufige Geofence-Entscheidung) ---
    // Score: +2 GW fehlt, +1 BLE-Target fehlt, +1 WiFi-Home fehlt → max 4
    uint8_t  evidenceWarnScore      = 2;     // Score >= 2 → CHECK/WARNING
    uint8_t  evidenceLostScore      = 4;     // Score >= 4 → sofort LOST
};

// PRODUCTION-Profile anwenden
inline void applyProductionProfile(TrackerConfig& c) {
    c.profile              = PowerProfile::PRODUCTION;
    c.beaconInterval_s     = 60;
    c.wakeInterval_s       = 300;
    c.ackWindow_ms         = 600;
    c.cmdWindow_ms         = 300;
    c.bleScanDuration_ms   = 2000;
    c.wifiScanDuration_ms  = 3000;
    c.activeTime_ms        = 10000;
    c.sleepTime_s          = 300;
    c.lora433Enabled       = true;
    c.lora868Enabled       = true;
    c.wifiEnabled          = true;   // WiFi-Scan für Geofence-Evidenz
    c.beaconIntervalLost_s = 30;     // PROD: schneller im LOST
    // deepSleepEnabled bleibt false bis explizit freigegeben
}

// TEST-Profile anwenden
inline void applyTestProfile(TrackerConfig& c) {
    c.profile              = PowerProfile::TEST;
    c.beaconInterval_s     = 5;
    c.wakeInterval_s       = 60;
    c.ackWindow_ms         = 600;
    c.cmdWindow_ms         = 400;
    c.bleScanDuration_ms   = 3000;
    c.wifiScanDuration_ms  = 5000;
    c.activeTime_ms        = 30000;
    c.sleepTime_s          = 60;
    c.lora433Enabled       = true;
    c.lora868Enabled       = true;
    c.bleEnabled           = true;
    c.bleGeofenceEnabled   = true;
    c.wifiEnabled          = true;   // WiFi-Scan für Geofence-Evidenz
    c.beaconIntervalLost_s = 10;     // TEST: schneller im LOST
    c.deepSleepEnabled     = false;
    c.lightSleepEnabled    = false;
}
