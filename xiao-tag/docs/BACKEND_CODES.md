# Backend codes

## Readiness codes
- `framework-disabled`
- `waiting-radio`
- `missing-secrets`
- `adapter-no-lib`
- `adapter-lib-only`
- `slot-not-ready`
- `real-slot-ready`
- `join-start-scaffold`
- `join-start-real-slot`
- `simulated-join`
- `real-join-slot-pending`
- `real-join-blocked`

## Error codes
- `none`
- `sx1262-not-ready`
- `otaa-format-invalid`
- `radiolib-header-missing`
- `real-backend-disabled`
- `slot-object-missing`
- `slot-not-fully-implemented`

These codes are surfaced through:
- serial command `backend`
- serial command `backendjson`
- serial command `preflight`
- serial command `preflightjson`
- telemetry JSON
