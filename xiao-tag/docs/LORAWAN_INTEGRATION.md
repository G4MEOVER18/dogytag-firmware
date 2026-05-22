# LoRaWAN Integration

## v1.5.0 status
The project now contains a staged RadioLib backend adapter.

### What is real now
- SX1262 hardware bring-up path exists.
- backend selection is explicit.
- compile-time RadioLib header detection exists.
- an optional PlatformIO environment exists for future RadioLib-enabled builds.

### What is still not real
- no actual OTAA join is executed yet
- no uplink/downlink path exists yet
- `lorawan_backend_radiolib.cpp` is still an adapter slot, not a finished RadioLib implementation

## Optional build environment
Use the dedicated environment once you want to try real RadioLib integration:

```bash
pio run -e xiao_esp32s3_radiolib
```

That environment adds `jgromes/RadioLib` and defines `SMARTTAG_ENABLE_RADIOLIB=1`.

## Recommended next implementation step
1. keep the current adapter boundary stable
2. add actual SX1262 module object creation inside `lorawan_backend_radiolib.cpp`
3. add OTAA join call behind the existing `start_join()` function
4. report exact RadioLib return/error codes through telemetry and serial logs
