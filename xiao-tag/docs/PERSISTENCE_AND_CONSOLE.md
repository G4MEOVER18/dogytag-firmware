# Persistence and serial console

## Runtime beacon text
The SX1278 test beacon payload can now be changed at runtime and stored in NVS.

### Console commands
- `beacon get`
- `beacon set <text>`
- `beacon save`
- `beacon reset`
- `beacon`
- `preflight`

## Storage
The firmware uses ESP32 `Preferences` with namespace `smarttag` and key `beacon`.

## Notes
- the payload is limited to 64 characters
- changes are only persistent after `beacon save`
- `beacon reset` restores the compiled default from `config_defaults.h`


## New in v2.3.0
- `cfgjson` emits runtime beacon/autojoin config as JSON.
- `preflightjson` emits join preflight as JSON.
- `autojoin` settings are now persistent via Preferences.
