#!/usr/bin/env bash
set -euo pipefail
repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
exec python3 "$repo/firmware/gas_sensor/server/receiver.py" --output "$repo/data" "$@"
