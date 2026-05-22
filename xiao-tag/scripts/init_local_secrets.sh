#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR"
if [ -f config/secrets.local.h ]; then
  echo "[INFO] config/secrets.local.h already exists"
  exit 0
fi
cp config/secrets.local.template.h config/secrets.local.h
echo "[OK] created config/secrets.local.h"
