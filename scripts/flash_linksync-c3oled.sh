#!/usr/bin/env bash
# Flash link-sync to an ACEIRMC / Super Mini ESP32-C3 0.42" OLED stamp.
#
# Isolated build dir + sdkconfig so this does not clobber an S3 tree.
# If esptool cannot auto-reset into the ROM bootloader:
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
  echo "No /dev/cu.usbmodem* found. Plug in the C3 USB-C cable." >&2
  exit 1
fi

BUILD_DIR="build-linksync-c3oled"
SDKCONFIG="sdkconfig.linksync-c3oled"
DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.linksync-c3oled"

need_reconfigure=0
if [[ ! -f "$SDKCONFIG" ]]; then
  need_reconfigure=1
elif ! grep -q 'CONFIG_IDF_TARGET="esp32c3"' "$SDKCONFIG"; then
  echo "sdkconfig is not an esp32c3 tree; reconfiguring..."
  rm -f "$SDKCONFIG" "${SDKCONFIG}.old"
  need_reconfigure=1
elif ! grep -q 'CONFIG_NEON_BOARD_LINKSYNC_C3OLED=y' "$SDKCONFIG"; then
  echo "sdkconfig is not the C3 OLED overlay; reconfiguring..."
  rm -f "$SDKCONFIG" "${SDKCONFIG}.old"
  need_reconfigure=1
fi

idf=(idf.py -B "$BUILD_DIR" -DSDKCONFIG="$SDKCONFIG" -DSDKCONFIG_DEFAULTS="$DEFAULTS")

if [[ "$need_reconfigure" -eq 1 ]]; then
  "${idf[@]}" set-target esp32c3
fi

echo "Building link-sync C3 OLED firmware..."
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

# 4 MB table: ota_0 @ 0x20000, ota_1 @ 0x200000.
if [[ "${OTA_0_OFF}" -ne $((0x20000)) || "${OTA_1_OFF}" -ne $((0x200000)) ]]; then
  echo "Refusing to flash: built table is not the 4 MB C3 layout." >&2
  printf '  ota_0=0x%x ota_1=0x%x (want 0x20000 / 0x200000)\n' \
    "${OTA_0_OFF}" "${OTA_1_OFF}" >&2
  echo "Delete $SDKCONFIG and re-run this script." >&2
  exit 1
fi

echo "Flashing to $PORT ..."
echo "  otadata @ $(printf '0x%x' "${OTADATA_OFF}")"
echo "  ota_0   @ $(printf '0x%x' "${OTA_0_OFF}")"
echo "  ota_1   @ $(printf '0x%x' "${OTA_1_OFF}")  (mirror — rollback must not hit erased flash)"

python -m esptool --chip esp32c3 -p "$PORT" -b 460800 \
  --before default_reset --after watchdog_reset \
  write_flash --flash_mode dio --flash_freq 80m --flash_size 4MB \
  0x0 "$BUILD_DIR/bootloader/bootloader.bin" \
  0x8000 "$BUILD_DIR/partition_table/partition-table.bin" \
  "$(printf '0x%x' "${OTADATA_OFF}")" "$BUILD_DIR/ota_data_initial.bin" \
  "$(printf '0x%x' "${OTA_0_OFF}")" "$BUILD_DIR/neon_link.bin" \
  "$(printf '0x%x' "${OTA_1_OFF}")" "$BUILD_DIR/neon_link.bin"

echo "Waiting for app_main after reset..."
if python3 - "$PORT" <<'PY'
import sys, time
try:
    import serial
except ImportError:
    print("pyserial not available; skip boot check", file=sys.stderr)
    sys.exit(0)

port = sys.argv[1]
ser = serial.Serial()
ser.port = port
ser.baudrate = 115200
ser.timeout = 0.25
ser.dsrdtr = False
ser.rtscts = False
ser.dtr = False
ser.rts = False
ser.open()
ser.dtr = False
ser.rts = False
deadline = time.time() + 25
buf = bytearray()
ok = False
while time.time() < deadline:
    chunk = ser.read(512)
    if chunk:
        buf.extend(chunk)
        if b"app_main enter" in buf or b"OTA image marked valid" in buf:
            ok = True
            break
ser.close()
sys.stdout.buffer.write(buf)
sys.stdout.buffer.write(b"\n")
if not ok:
    print(
        "BOOT CHECK FAILED: no app_main breadcrumb. "
        "Press RESET only (not BOOT).",
        file=sys.stderr,
    )
    sys.exit(1)
print("BOOT CHECK OK")
PY
then
  echo "Done. Monitor with: idf.py -B $BUILD_DIR -DSDKCONFIG=$SDKCONFIG -p $PORT monitor"
  echo "First boot: join the SoftAP shown on the OLED (SETUP page) and open http://192.168.4.1"
else
  echo "Flash wrote, but the app did not announce itself." >&2
  echo "Press RESET once (not BOOT) and watch the OLED." >&2
  exit 1
fi
