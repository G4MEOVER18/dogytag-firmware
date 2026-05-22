#!/usr/bin/env python3
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[1]
local = root / 'config' / 'secrets.local.h'
example = root / 'config' / 'secrets.example.h'
path = local if local.exists() else example
text = path.read_text()
patterns = {
    'LORAWAN_DEV_EUI': 16,
    'LORAWAN_JOIN_EUI': 16,
    'LORAWAN_APP_KEY': 32,
}
for key, length in patterns.items():
    m = re.search(rf'{key}\s*=\s*"([^"]+)"', text)
    if not m:
        print(f'[FAIL] missing {key} in {path.name}')
        sys.exit(1)
    value = m.group(1)
    if 'REPLACE_' in value:
        print(f'[WARN] {key} still placeholder in {path.name}')
        continue
    if len(value) != length or re.fullmatch(r'[0-9A-Fa-f]+', value) is None:
        print(f'[FAIL] {key} must be {length} hex chars')
        sys.exit(1)
    print(f'[OK] {key} format valid')
print(f'[DONE] checked {path.name}')
