#!/usr/bin/env bash
# Flash link-sync (CrowPanel / Waveshare 5.79" e-Paper + ESP32-S3).
# Isolated build dir — will not overwrite P4 / Tab5 / root sdkconfig.
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
  PORT="$(ls /dev/cu.usbserial* 2>/dev/null | head -1 || true)"
fi
if [[ -z "$PORT" ]]; then
  PORT="$(ls /dev/cu.usbmodem* 2>/dev/null | head -1 || true)"
fi
if [[ -z "$PORT" ]]; then
  echo "No CH343 (cu.usbserial*) or USB-JTAG (cu.usbmodem*). Plug in the panel." >&2
  exit 1
fi

BUILD_DIR="build-linksync-epd"
SDKCONFIG="$BUILD_DIR/sdkconfig"
LS_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.linksync-epd"

need_reconfigure=0
if [[ ! -f "$SDKCONFIG" ]]; then
  need_reconfigure=1
elif ! grep -q 'CONFIG_NEON_BOARD_LINKSYNC_EPD=y' "$SDKCONFIG"; then
  echo "isolated sdkconfig is not the link-sync-epd board; reconfiguring..."
  need_reconfigure=1
fi

if [[ "$need_reconfigure" -eq 1 ]]; then
  idf.py -B "$BUILD_DIR" -DSDKCONFIG="$SDKCONFIG" \
    -DSDKCONFIG_DEFAULTS="$LS_DEFAULTS" set-target esp32s3
  idf.py -B "$BUILD_DIR" -DSDKCONFIG="$SDKCONFIG" \
    -DSDKCONFIG_DEFAULTS="$LS_DEFAULTS" reconfigure
fi

echo "Building link-sync-epd (isolated $BUILD_DIR)..."
idf.py -B "$BUILD_DIR" -DSDKCONFIG="$SDKCONFIG" \
  -DSDKCONFIG_DEFAULTS="$LS_DEFAULTS" build

python3 - "$BUILD_DIR/partition_table/partition-table.bin" "$BUILD_DIR/ota_offsets.env" <<'PY'
import struct, sys
path, out_path = sys.argv[1], sys.argv[2]
data = open(path, "rb").read()
slots = {}
off = 0
while off + 32 <= len(data):
    magic, _type, subtype, offset, size = struct.unpack_from("<HBBII", data, off)
    if magic == 0xFFFF:
        break
    if magic != 0x50AA:
        break
    label = data[off + 12 : off + 28].split(b"\x00", 1)[0].decode("ascii", "replace")
    slots[label] = (offset, size)
    off += 32
missing = [n for n in ("ota_0", "ota_1", "otadata") if n not in slots]
if missing:
    sys.stderr.write("partition table is missing %s\n" % ", ".join(missing))
    sys.exit(1)
lines = []
for name in ("ota_0", "ota_1", "otadata"):
    key = name.upper().replace("-", "_")
    lines.append("%s_OFF=%d" % (key, slots[name][0]))
    lines.append("%s_SIZE=%d" % (key, slots[name][1]))
open(out_path, "w").write("\n".join(lines) + "\n")
PY
# shellcheck disable=SC1091
source "$BUILD_DIR/ota_offsets.env"

if [[ "${OTA_0_OFF}" -ne $((0x20000)) || "${OTA_1_OFF}" -ne $((0x320000)) ]]; then
  echo "Refusing to flash: built table is not the 8 MB layout." >&2
  exit 1
fi

echo "Flashing to $PORT ..."
python -m esptool --chip esp32s3 -p "$PORT" -b 460800 \
  --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_freq 80m --flash_size 8MB \
  0x0 "$BUILD_DIR/bootloader/bootloader.bin" \
  0x8000 "$BUILD_DIR/partition_table/partition-table.bin" \
  "$(printf '0x%x' "${OTADATA_OFF}")" "$BUILD_DIR/ota_data_initial.bin" \
  "$(printf '0x%x' "${OTA_0_OFF}")" "$BUILD_DIR/neon_link.bin" \
  "$(printf '0x%x' "${OTA_1_OFF}")" "$BUILD_DIR/neon_link.bin"

echo "Done. Monitor with: idf.py -B $BUILD_DIR -p $PORT monitor"
