# Compilation status

## v1.2.0
This package is internally checked for structure and consistency.
A previous real PlatformIO build attempt in the packaging environment was blocked before compilation by a platform download failure (`HTTPClientError` while installing `espressif32`).

## Required local verification
Run locally in VS Code / PlatformIO:

```bash
pio run --target clean && pio run
```

Treat the local compiler output as the source of truth for final build verification.
