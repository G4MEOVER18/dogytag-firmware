# Build and Flash

## Default build
```bash
pio run --target clean && pio run
```

## Default upload
```bash
pio run --target upload
```

## Serial monitor
```bash
pio device monitor
```

## Optional RadioLib preparation build
```bash
pio run -e xiao_esp32s3_radiolib
```

This optional environment is intended for the future real LoRaWAN backend step.


See also `docs/SERIAL_COMMANDS.md` for live monitor commands.


## v2.1.0 additions
- serial command `preflight`
- serial commands `beacon get`, `beacon set <text>`, `beacon save`, `beacon reset`
- beacon payload stored via ESP32 Preferences


## v2.2.0 additions
- serial command `keys` for OTAA key-format diagnostics
- build matrix now runs secrets format checks before PlatformIO builds
