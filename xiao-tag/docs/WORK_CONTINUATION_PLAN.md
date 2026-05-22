# Work continuation plan

## Version 1.7.0 status
- main.cpp status output corrected
- SX1278 RX smoke path added
- RadioLib backend scaffold states clarified

## Next technical step
- 1.8.0: add local build matrix helper and stricter environment notes
- 1.9.0: first real RadioLib object wiring attempt behind compile guards
- 2.0.0: real LoRaWAN join path and SX1278 packet TX/RX validation


## Additional local checks
- `python scripts/check_secrets_format.py`
- ensure OTAA values are hex and correct length before any real backend join work


See also `docs/SERIAL_COMMANDS.md` for live monitor commands.


## v2.1.0 additions
- serial command `preflight`
- serial commands `beacon get`, `beacon set <text>`, `beacon save`, `beacon reset`
- beacon payload stored via ESP32 Preferences


## v2.2.0 additions
- serial command `keys` for OTAA key-format diagnostics
- build matrix now runs secrets format checks before PlatformIO builds


## Added in v2.4.0
- Runtime config is now serial-adjustable and persistent.
- JSON console outputs (`cfgjson`, `preflightjson`) are available for agents and tooling.
- Next priority remains real SX1262 backend integration and actual LoRaWAN join calls.
