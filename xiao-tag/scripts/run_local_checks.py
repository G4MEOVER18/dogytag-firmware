#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
REQUIRED = [
    ROOT / 'platformio.ini',
    ROOT / 'VERSION',
    ROOT / 'README.md',
    ROOT / 'include' / 'pins.h',
    ROOT / 'include' / 'config_defaults.h',
    ROOT / 'src' / 'main.cpp',
    ROOT / 'src' / 'radio_sx1278.cpp',
    ROOT / 'src' / 'radio_sx1262.cpp',
    ROOT / 'src' / 'gps.cpp',
    ROOT / 'src' / 'command_console.cpp',
    ROOT / 'src' / 'runtime_config.cpp',
    ROOT / 'src' / 'lorawan_manager.cpp',
    ROOT / 'prompt.md',
]

VERSION_TOKEN = (ROOT / 'VERSION').read_text().strip()


def fail(msg: str) -> None:
    print(f'[FAIL] {msg}')
    sys.exit(1)


def check_probable_broken_printf(file: Path) -> list[str]:
    issues: list[str] = []
    lines = file.read_text().splitlines()
    for idx, line in enumerate(lines, start=1):
        if 'Serial.printf(' not in line and 'snprintf(' not in line and 'std::snprintf(' not in line:
            continue
        if 'payload="' in line or 'text="' in line or '{"' in line:
            issues.append(f'{file.relative_to(ROOT)}:{idx}')
    return issues


def main() -> int:
    for path in REQUIRED:
        if not path.exists():
            fail(f'missing required file: {path.relative_to(ROOT)}')

    build_info = (ROOT / 'include' / 'build_info.h').read_text()
    if VERSION_TOKEN not in build_info:
        fail('VERSION and include/build_info.h are out of sync')

    changelog = (ROOT / 'CHANGELOG.md').read_text()
    if VERSION_TOKEN not in changelog:
        fail('VERSION and CHANGELOG.md are out of sync')

    main_cpp = (ROOT / 'src' / 'main.cpp').read_text()
    for marker in ['radio_sx1262::begin();', 'radio_sx1278::begin();', 'gps::begin();', 'timebase::begin();']:
        if marker not in main_cpp:
            fail(f'missing main.cpp bring-up call: {marker}')
    if 'const auto backend =' in main_cpp:
        fail('stale backend variable alias found in src/main.cpp')

    bad_hits: list[str] = []
    for file in sorted((ROOT / 'src').glob('*.cpp')):
        bad_hits.extend(check_probable_broken_printf(file))
    if bad_hits:
        fail('probable unescaped string literal(s): ' + ', '.join(bad_hits))

    print('[OK] project structure present')
    print(f'[OK] version synchronized: {VERSION_TOKEN}')
    print('[OK] bring-up entry points found in src/main.cpp')
    print('[OK] no stale backend variable alias in src/main.cpp')
    print('[OK] no probable unescaped string literals detected')
    print('[NEXT] run: pio run --target clean && pio run')
    print('[INFO] optional env available: xiao_esp32s3_radiolib')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
