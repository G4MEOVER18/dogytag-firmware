# Environments

## Default
- `xiao_esp32s3`
- no explicit RadioLib compile define

## Optional RadioLib prep build
- `xiao_esp32s3_radiolib`
- enables `SMARTTAG_ENABLE_RADIOLIB=1`
- intended for backend slot preparation, not yet a guaranteed final join build

## Recommended local sequence
```bash
python scripts/validate_project.py
python scripts/run_local_checks.py
python scripts/check_secrets_format.py
sh scripts/build_matrix.sh
```
