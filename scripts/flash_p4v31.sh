#!/usr/bin/env bash
# Flash NEON LINK to the second P4 DUT (rev v3.1 / eco6) with IDF 5.5.5.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if [[ -z "${IDF_PATH:-}" ]]; then
  if [[ -f "$HOME/esp/esp-idf-v5.5.5/export.sh" ]]; then
    # shellcheck disable=SC1091
    source "$HOME/esp/esp-idf-v5.5.5/export.sh"
  else
    echo "ESP-IDF v5.5.5 not found. Install it next to v5.3.2 and source export.sh." >&2
    exit 1
  fi
fi

PORT="${1:-}"
if [[ -z "$PORT" ]]; then
  PORT="$(ls /dev/cu.usbmodem* 2>/dev/null | head -1 || true)"
fi
if [[ -z "$PORT" ]]; then
  echo "No /dev/cu.usbmodem* found." >&2
  exit 1
fi

# This adapter dropped packets at 460800 during the A1 bring-up.
idf.py -B build-p4v31 -DSDKCONFIG=sdkconfig.p4v31 \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.p4v31" \
  -p "$PORT" -b 115200 flash
echo "Done. Setup AP is NEON-LINK-XXXX (password on the OLED NETWORK screen, default link1234)."
