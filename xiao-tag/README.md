# smarttag-firmware

Firmware basis for Seeed Studio XIAO ESP32S3 with:
- Wio-SX1262 (868 MHz / LoRaWAN path)
- RA-01 / SX1278 (433 MHz test path)
- GPS with UART + PPS
- optional WiFi/MQTT diagnostics

## Current version
1.7.0

## Current focus
- SX1262 bring-up and backend preparation for LoRaWAN
- SX1278 smoke, TX, and RX mode diagnostics
- GPS/PPS timebase groundwork

## Recommended local workflow
```bash
python scripts/validate_project.py
python scripts/run_local_checks.py
sh scripts/init_local_secrets.sh
pio run --target clean && pio run
pio run --target upload
pio device monitor
```


## New in v1.9.0
- LoRaWAN key validation and byte parsing scaffold
- backend hardware slot scaffold for future real RadioLib binding


## New in v2.0.0
- serial command console for live bring-up
- manual join/beacon/status test flow from serial monitor


## v2.1.0 additions
- serial command `preflight`
- serial commands `beacon get`, `beacon set <text>`, `beacon save`, `beacon reset`
- beacon payload stored via ESP32 Preferences


## v2.3.0 additions
- serial command `keys` for OTAA key-format diagnostics
- build matrix now runs secrets format checks before PlatformIO builds


## Serial console quick commands
- `help`
- `status`
- `backend`
- `backendjson`
- `preflight`
- `preflightjson`
- `join`
- `beacon get|set|save|reset`
- `autojoin get|on|off|save|reset`


Zusatz in v2.6.1: `version`, `uptime`, `console`.
