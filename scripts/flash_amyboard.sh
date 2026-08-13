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

# Always rebuild so a leftover neon_link.bin cannot flash yesterday's
# tree. export.sh above is what puts idf.py on PATH.
echo "Building AMYboard firmware..."
idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.amyboard" build

echo "Flashing to $PORT ..."
# OTA partition table: app at 0x20000 (not the old 0x10000). The same
# image is written to ota_1 (0x620000) so a rollback cannot land on
# erased flash — that is the blank OLED / two-pixels-lit brick after
# a USB flash + reboot.
python -m esptool --chip esp32s3 -p "$PORT" -b 460800 \
  --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB \
  0x0 build/bootloader/bootloader.bin \
  0x8000 build/partition_table/partition-table.bin \
  0xf000 build/ota_data_initial.bin \
  0x20000 build/neon_link.bin \
  0x620000 build/neon_link.bin

echo "Done. Monitor with: idf.py -p $PORT monitor"
