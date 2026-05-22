#!/usr/bin/env python3
from pathlib import Path
import sys
ROOT = Path(__file__).resolve().parents[1]
REQUIRED = [
    ROOT / 'platformio.ini', ROOT / 'VERSION', ROOT / 'README.md', ROOT / 'prompt.md',
    ROOT / 'include' / 'pins.h', ROOT / 'include' / 'config_defaults.h', ROOT / 'include' / 'build_info.h',
    ROOT / 'src' / 'main.cpp', ROOT / 'src' / 'radio_sx1262.cpp', ROOT / 'src' / 'radio_sx1278.cpp',
    ROOT / 'src' / 'gps.cpp', ROOT / 'src' / 'timebase.cpp', ROOT / 'src' / 'command_console.cpp', ROOT / 'docs' / 'HARDWARE.md'
]
for p in REQUIRED:
    if not p.exists():
        print(f'[FAIL] missing {p.relative_to(ROOT)}')
        sys.exit(1)
version = (ROOT / 'VERSION').read_text().strip()
if version not in (ROOT / 'include' / 'build_info.h').read_text():
    print('[FAIL] build_info.h version mismatch')
    sys.exit(1)
print(f'[OK] structure valid, version {version}')
