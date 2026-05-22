# Architecture

## Modules
- `gps.*` reads UART NMEA and exposes GPS state
- `timebase.*` binds PPS events to GPS UTC state
- `radio_sx1262.*` contains SX1262 bring-up and smoke diagnostics
- `radio_sx1278.*` contains SX1278 register/mode smoke diagnostics
- `lorawan_manager.*` is an adapter scaffold for later LoRaWAN library integration
- `wifi_manager.*` and `mqtt_client.*` are optional infrastructure paths
- `telemetry.*` builds JSON snapshots for logs/MQTT
- `app.*` keeps heartbeat level application behavior

## Priority order
1. SX1262 real SPI/command health
2. LoRaWAN library integration
3. SX1278 tx/rx path
4. GPS/PPS high precision timebase
5. MQTT/telemetry hardening
