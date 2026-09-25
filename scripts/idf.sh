#!/usr/bin/env bash
set -euo pipefail
repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
if ! command -v idf.py >/dev/null; then
  echo 'Open an ESP-IDF terminal or activate your ESP-IDF installation first.' >&2
  exit 1
fi
cd "$repo/firmware/gas_sensor"
exec idf.py "$@"
