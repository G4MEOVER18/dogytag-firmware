#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR"
VERSION="$(tr -d '[:space:]' < VERSION)"
OUT_NAME="smarttag-firmware-v${VERSION}.zip"
rm -f "$OUT_NAME"
zip -qr "$OUT_NAME" . -x '*.git*' -x '.pio/*' -x 'config/secrets.local.h' -x '__pycache__/*'
echo "[OK] wrote $OUT_NAME"
