#pragma once
// =============================================================
// DogyTag — Command IDs & Radio Protocol Definitions
// Shared between Tag and Gateway
// Protocol Version: 2
// =============================================================

// --- Radio Frame Magic / Protocol ---
#define PROTO_VERSION        0x02
#define FRAME_END_MARKER     0xAB

// --- Paket-Typen (Byte 0) ---
#define PKT_BEACON           0x42   // 'B' Tag→GW, 11B
#define PKT_ACK_BYTE         0x41   // 'A' GW→Tag, 9B
#define PKT_LOG_BYTE         0x4C   // 'L' Tag→GW, 12B
#define PKT_CMD              0x43   // 'C' GW→Tag, Command
#define PKT_RESP             0x52   // 'R' Tag→GW, Response
#define PKT_GPS              0x47   // 'G' Tag→GW, 20B GPS Fix
// GPS Frame Layout:
// [0x47][ID 6B][lat_i32 4B][lon_i32 4B][alt_i16 2B][sats 1B][hdop*10 1B][0xAB] = 20B
// lat/lon: degrees * 1e6 (int32), alt: Meter (int16), hdop: hdop*10 (uint8, max 25.5)

// --- CMD Frame Layout V2 ---
// Byte 0:    PKT_CMD (0x43)
// Byte 1:    PROTO_VERSION (0x02)
// Byte 2:    FLAGS  (bit0=requiresAck, bit1=broadcast, bit2=highPrio)
// Byte 3:    SEQ_HI
// Byte 4:    SEQ_LO
// Byte 5:    CMD_ID
// Byte 6:    PARAM_LEN
// Byte 7..N: PARAMS
// Byte N+1:  CRC_HI  (CRC16-CCITT over bytes 0..N)
// Byte N+2:  CRC_LO
// Byte N+3:  FRAME_END_MARKER (0xAB)
//
// Min frame size: 8 bytes (no params)
// Max params: 48 bytes

// --- RESP Frame Layout V2 ---
// Byte 0:    PKT_RESP (0x52)
// Byte 1:    PROTO_VERSION (0x02)
// Byte 2:    SEQ_HI  (echo from CMD)
// Byte 3:    SEQ_LO
// Byte 4:    CMD_ID  (echo)
// Byte 5:    RESULT_CODE
// Byte 6:    RESP_LEN
// Byte 7..N: RESP_DATA (ASCII)
// Byte N+1:  CRC_HI
// Byte N+2:  CRC_LO
// Byte N+3:  FRAME_END_MARKER

// --- FLAGS bits ---
#define FLAG_REQUIRES_ACK    0x01
#define FLAG_BROADCAST       0x02
#define FLAG_HIGH_PRIO       0x04

// --- Result Codes ---
#define RC_OK                0x00
#define RC_ERR_UNKNOWN_CMD   0x01
#define RC_ERR_PARAM_LEN     0x02
#define RC_ERR_PARAM_RANGE   0x03
#define RC_ERR_NOT_SUPPORTED 0x04
#define RC_ERR_BUSY          0x05
#define RC_ERR_AUTH          0x06
#define RC_ERR_CRC           0x07
#define RC_PENDING           0x10   // Async (z.B. GPS fix kommt später)
#define RC_ERR_TIMEOUT       0x11

// --- CMD IDs ---
#define CMD_SET_BEACON_INTERVAL    0x01
#define CMD_SET_WAKE_INTERVAL      0x02
#define CMD_SET_ACK_WINDOW         0x03
#define CMD_SET_PROFILE            0x04
#define CMD_SET_STATE              0x05
#define CMD_TRIGGER_LOST           0x06
#define CMD_TRIGGER_GPS            0x07
#define CMD_TRIGGER_RECOVERY       0x08
#define CMD_TRIGGER_NORMAL         0x09
#define CMD_FORCE_LORAWAN_JOIN     0x0A
#define CMD_SET_868_SF             0x0B
#define CMD_SET_868_TX_POWER       0x0C
#define CMD_SET_433_SF             0x0D
#define CMD_ENABLE_MODULE          0x0E
#define CMD_DISABLE_MODULE         0x0F
#define CMD_BLE_SCAN               0x10
#define CMD_SET_BLE_GEOFENCE       0x11
#define CMD_CLEAR_BLE_GEOFENCE     0x12
#define CMD_WIFI_SCAN              0x13
#define CMD_SET_WIFI_CREDENTIALS   0x14
#define CMD_GPS_TRIGGER            0x15
#define CMD_GPS_STOP               0x16
#define CMD_GET_STATUS             0x17
#define CMD_GET_BATTERY            0x18
#define CMD_GET_CONFIG             0x19
#define CMD_PING                   0x1A
#define CMD_REBOOT                 0x1B
#define CMD_FACTORY_RESET          0x1C
#define CMD_SAVE_CONFIG            0x1D
#define CMD_SET_BLE_SCAN_DURATION  0x1E
#define CMD_SET_ACTIVE_TIME        0x1F
#define CMD_GET_RSSI               0x20
#define CMD_GET_BLE_GEOFENCE       0x21
#define CMD_SET_GW_TIMEOUT         0x22
#define CMD_WIFI_CONNECT           0x23
#define CMD_WIFI_DISCONNECT        0x24
#define CMD_GPS_CONTINUOUS         0x25
#define CMD_SET_WARNING_TIMEOUT    0x26
#define CMD_GET_STATE              0x27
#define CMD_SET_DEVICE_ID          0x28
#define CMD_TRIGGER_CHECK          0x29
#define CMD_LOAD_CONFIG            0x2A

// --- Module IDs ---
#define MOD_BLE                    0x01
#define MOD_WIFI                   0x02
#define MOD_GPS                    0x03
#define MOD_LORA868                0x04
#define MOD_LORA433                0x05
#define MOD_LORAWAN                0x06

// --- State Enum (auch als uint8 über Radio) ---
#define STATE_DEEP_SLEEP           0x00
#define STATE_NORMAL               0x01
#define STATE_CHECK                0x02
#define STATE_WARNING              0x03
#define STATE_LOST                 0x04
#define STATE_PRECISION            0x05
#define STATE_RECOVERY             0x06

// --- Profile ---
#define PROFILE_TEST               0x00
#define PROFILE_PRODUCTION         0x01

// --- CRC16-CCITT ---
inline uint16_t crc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000) crc = (crc << 1) ^ 0x1021;
            else              crc <<= 1;
        }
    }
    return crc;
}

// --- Frame Builder ---
// Baut CMD-Frame V2 zusammen. Gibt Länge zurück, 0 bei Fehler.
inline size_t buildCmdFrame(uint8_t* out, size_t maxLen,
                            uint8_t cmdId, uint16_t seq, uint8_t flags,
                            const uint8_t* params, uint8_t paramLen) {
    size_t frameLen = 7 + paramLen + 3;  // header(7) + params + crc(2) + end(1)
    if (frameLen > maxLen) return 0;
    out[0] = PKT_CMD;
    out[1] = PROTO_VERSION;
    out[2] = flags;
    out[3] = (seq >> 8) & 0xFF;
    out[4] = seq & 0xFF;
    out[5] = cmdId;
    out[6] = paramLen;
    if (paramLen && params) memcpy(out + 7, params, paramLen);
    uint16_t crc = crc16(out, 7 + paramLen);
    out[7 + paramLen] = (crc >> 8) & 0xFF;
    out[8 + paramLen] = crc & 0xFF;
    out[9 + paramLen] = FRAME_END_MARKER;
    return frameLen;
}

// --- Frame Parser ---
inline bool parseCmdFrame(const uint8_t* data, size_t len,
                          uint8_t& cmdId, uint16_t& seq, uint8_t& flags,
                          const uint8_t** params, uint8_t& paramLen) {
    if (len < 10) return false;
    if (data[0] != PKT_CMD)         return false;
    if (data[1] != PROTO_VERSION)   return false;
    if (data[len - 1] != FRAME_END_MARKER) return false;
    flags    = data[2];
    seq      = ((uint16_t)data[3] << 8) | data[4];
    cmdId    = data[5];
    paramLen = data[6];
    if ((size_t)(7 + paramLen + 3) != len) return false;
    *params  = data + 7;
    // CRC check
    uint16_t rxCrc  = ((uint16_t)data[7 + paramLen] << 8) | data[8 + paramLen];
    uint16_t calcCrc = crc16(data, 7 + paramLen);
    if (rxCrc != calcCrc) return false;
    return true;
}

// --- RESP Frame Builder ---
inline size_t buildRespFrame(uint8_t* out, size_t maxLen,
                             uint8_t cmdId, uint16_t seq, uint8_t resultCode,
                             const char* respStr) {
    uint8_t respLen = respStr ? (uint8_t)strnlen(respStr, 48) : 0;
    size_t frameLen = 7 + respLen + 3;
    if (frameLen > maxLen) return 0;
    out[0] = PKT_RESP;
    out[1] = PROTO_VERSION;
    out[2] = (seq >> 8) & 0xFF;
    out[3] = seq & 0xFF;
    out[4] = cmdId;
    out[5] = resultCode;
    out[6] = respLen;
    if (respLen) memcpy(out + 7, respStr, respLen);
    uint16_t crc = crc16(out, 7 + respLen);
    out[7 + respLen] = (crc >> 8) & 0xFF;
    out[8 + respLen] = crc & 0xFF;
    out[9 + respLen] = FRAME_END_MARKER;
    return frameLen;
}
