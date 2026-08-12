#!/usr/bin/env bash
# Flash NEON LINK (AMYboard profile) over USB-C.
#
# If esptool cannot auto-reset into the ROM bootloader (common after a
# prior firmware has taken over USB), put the board in DFU manually:
#   1. Hold BOOT + RST on the back of the AMYboard
#   2. Release RST first, then BOOT
#   3. Re-run this script within a few seconds
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if [[ -z "${IDF_PATH:-}" ]]; then
  if [[ -f "$HOME/esp/esp-idf-v5.3.2/export.sh" ]]; then
    # shellcheck disable=SC1091
    source "$HOME/esp/esp-idf-v5.3.2/export.sh"
  else
    echo "ESP-IDF not found. Install v5.3.2 and source export.sh first." >&2
    exit 1
  fi
fi

PORT="${1:-}"
if [[ -z "$PORT" ]]; then
  PORT="$(ls /dev/cu.usbmodem* 2>/dev/null | head -1 || true)"
fi
if [[ -z "$PORT" ]]; then
  echo "No /dev/cu.usbmodem* found. Plug in the AMYboard USB-C cable." >&2
  exit 1
fi

if [[ ! -f build/neon_link.bin ]]; then
  echo "Building AMYboard firmware..."
  idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.amyboard" set-target esp32s3
  idf.py build
fi

echo "Flashing to $PORT ..."
# OTA partition table: app at 0x20000 (not the old 0x10000). flash_args
# paths are relative to build/.
python -m esptool --chip esp32s3 -p "$PORT" -b 115200 \
  --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB \
  0x0 build/bootloader/bootloader.bin \
  0x8000 build/partition_table/partition-table.bin \
  0xf000 build/ota_data_initial.bin \
  0x20000 build/neon_link.bin

echo "Done. Monitor with: idf.py -p $PORT monitor"
