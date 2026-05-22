#!/usr/bin/env bash
set -euo pipefail
python scripts/validate_project.py
python scripts/run_local_checks.py
python scripts/check_secrets_format.py
pio run -e xiao_esp32s3
pio run -e xiao_esp32s3_radiolib || true
