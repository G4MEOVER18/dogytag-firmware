# Secrets format

## LoRaWAN OTAA fields
These values must be plain hexadecimal strings without spaces or separators.

- `LORAWAN_DEV_EUI`: 16 hex chars (8 bytes)
- `LORAWAN_JOIN_EUI`: 16 hex chars (8 bytes)
- `LORAWAN_APP_KEY`: 32 hex chars (16 bytes)

Examples:
- `0011223344556677`
- `89ABCDEF01234567`
- `00112233445566778899AABBCCDDEEFF`

## Local check
Run:

```bash
python scripts/check_secrets_format.py
```
