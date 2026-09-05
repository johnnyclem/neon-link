#!/usr/bin/env bash
# Flash ESP-Hosted UART slave onto a Seeed XIAO ESP32-C5 (the CrowPanel
# expansion radio). Uses the XIAO's own USB-C (usbmodem), never the
# CrowPanel CH343 and never the Waveshare RLCD.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PROJ="$ROOT/tools/xiao-c5-hosted-uart"

if [[ -z "${IDF_PATH:-}" ]]; then
  if [[ -f "$HOME/esp/esp-idf-v5.5.5/export.sh" ]]; then
    # shellcheck disable=SC1091
    source "$HOME/esp/esp-idf-v5.5.5/export.sh"
  else
    echo "ESP-IDF v5.5.5 not found." >&2
    exit 1
  fi
fi

PORT="${1:-}"
if [[ -z "$PORT" ]]; then
  PORT="$(ls /dev/cu.usbmodem* 2>/dev/null | head -1 || true)"
fi
if [[ -z "$PORT" ]]; then
  echo "No XIAO USB-Serial/JTAG (cu.usbmodem*). Plug in the C5 USB-C." >&2
  exit 1
fi
if [[ "$PORT" == *wchusbserial* ]]; then
  echo "REFUSE: $PORT is the CrowPanel CH343, not the XIAO C5." >&2
  exit 1
fi

IDENT="$(python -m esptool --port "$PORT" chip_id 2>&1 || true)"
echo "$IDENT"
echo "$IDENT" | grep -q 'Chip is ESP32-C5' || {
  echo "REFUSE: $PORT is not an ESP32-C5." >&2
  exit 1
}

cd "$PROJ"
idf.py set-target esp32c5
idf.py -p "$PORT" flash
echo "C5 Hosted UART slave flashed. Leave this USB plugged for logs."
echo "DIP on the CrowPanel must be WM. Tap RST on the XIAO after the P4 boots."
