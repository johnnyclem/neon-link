#!/usr/bin/env bash
# Flash link-sync to a Makerfabs MaTouch ESP32-S3 1.28" Rotary (N16R8).
#
# Isolated build dir + sdkconfig so this does not clobber another S3 tree.
# The Type-C port is native USB Serial/JTAG. If esptool cannot auto-reset
# into the ROM bootloader:
#   1. Hold BOOT
#   2. Tap RESET
#   3. Release BOOT
#   4. Re-run this script
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
  echo "No /dev/cu.usbmodem* found. Plug in the MaTouch USB-C cable." >&2
  exit 1
fi

BUILD_DIR="build-linksync-matouch"
SDKCONFIG="$BUILD_DIR/sdkconfig"
DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.linksync-matouch"

need_reconfigure=0
if [[ ! -f "$SDKCONFIG" ]]; then
  need_reconfigure=1
elif ! grep -q 'CONFIG_NEON_BOARD_LINKSYNC_MATOUCH=y' "$SDKCONFIG"; then
  echo "isolated sdkconfig is not the MaTouch board; reconfiguring..."
  need_reconfigure=1
fi

idf=(idf.py -B "$BUILD_DIR" -DSDKCONFIG="$SDKCONFIG" -DSDKCONFIG_DEFAULTS="$DEFAULTS")

if [[ "$need_reconfigure" -eq 1 ]]; then
  "${idf[@]}" set-target esp32s3
  "${idf[@]}" reconfigure
fi

echo "Building link-sync MaTouch firmware (isolated $BUILD_DIR)..."
"${idf[@]}" build

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

# 16 MB table: ota_0 @ 0x20000, ota_1 @ 0x620000.
if [[ "${OTA_0_OFF}" -ne $((0x20000)) || "${OTA_1_OFF}" -ne $((0x620000)) ]]; then
  echo "Refusing to flash: built table is not the 16 MB layout." >&2
  printf '  ota_0=0x%x ota_1=0x%x (want 0x20000 / 0x620000)\n' \
    "${OTA_0_OFF}" "${OTA_1_OFF}" >&2
  exit 1
fi

echo "Flashing to $PORT ..."
python -m esptool --chip esp32s3 -p "$PORT" -b 460800 \
  --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB \
  0x0 "$BUILD_DIR/bootloader/bootloader.bin" \
  0x8000 "$BUILD_DIR/partition_table/partition-table.bin" \
  "$(printf '0x%x' "${OTADATA_OFF}")" "$BUILD_DIR/ota_data_initial.bin" \
  "$(printf '0x%x' "${OTA_0_OFF}")" "$BUILD_DIR/neon_link.bin" \
  "$(printf '0x%x' "${OTA_1_OFF}")" "$BUILD_DIR/neon_link.bin"

echo "Done. Monitor with: idf.py -B $BUILD_DIR -p $PORT monitor"
echo "First boot: the round dial shows NEON, then the live tempo ring."
