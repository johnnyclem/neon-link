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

# -DSDKCONFIG_DEFAULTS is ignored when sdkconfig already exists. A leftover
# 8 MB table (ota_1 @ 0x320000) plus a hardcoded write at 0x620000 is the
# blank-OLED brick: rollback lands on an empty slot. Force the AMYboard
# overlay so the image, the table, and the write addresses agree.
AMY_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.amyboard"
need_reconfigure=0
if [[ ! -f sdkconfig ]]; then
  need_reconfigure=1
elif ! grep -q 'CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions_16mb.csv"' sdkconfig; then
  echo "sdkconfig is not the AMYboard 16 MB table; reconfiguring..."
  rm -f sdkconfig sdkconfig.old
  need_reconfigure=1
fi

if [[ "$need_reconfigure" -eq 1 ]]; then
  idf.py -DSDKCONFIG_DEFAULTS="$AMY_DEFAULTS" reconfigure
fi

echo "Building AMYboard firmware..."
idf.py -DSDKCONFIG_DEFAULTS="$AMY_DEFAULTS" build

# Read slot offsets from the table we just built — never hardcode them.
# Write a tiny env file and source it; eval+heredoc trips bash 3.2.
python3 - "$ROOT/build/partition_table/partition-table.bin" "$ROOT/build/ota_offsets.env" <<'PY'
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
source "$ROOT/build/ota_offsets.env"

# AMYboard is 16 MB / 6 MB slots. Anything else is the 8 MB table and the
# next rollback will look like a dead panel again.
if [[ "${OTA_1_OFF}" -ne $((0x620000)) || "${OTA_0_OFF}" -ne $((0x20000)) ]]; then
  echo "Refusing to flash: built table is not the AMYboard 16 MB layout." >&2
  printf '  ota_0=0x%x ota_1=0x%x (want 0x20000 / 0x620000)\n' \
    "${OTA_0_OFF}" "${OTA_1_OFF}" >&2
  echo "Delete sdkconfig and re-run this script." >&2
  exit 1
fi

echo "Flashing to $PORT ..."
echo "  otadata @ $(printf '0x%x' "${OTADATA_OFF}")"
echo "  ota_0   @ $(printf '0x%x' "${OTA_0_OFF}")"
echo "  ota_1   @ $(printf '0x%x' "${OTA_1_OFF}")  (mirror — rollback must not hit erased flash)"

python -m esptool --chip esp32s3 -p "$PORT" -b 460800 \
  --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB \
  0x0 build/bootloader/bootloader.bin \
  0x8000 build/partition_table/partition-table.bin \
  "$(printf '0x%x' "${OTADATA_OFF}")" build/ota_data_initial.bin \
  "$(printf '0x%x' "${OTA_0_OFF}")" build/neon_link.bin \
  "$(printf '0x%x' "${OTA_1_OFF}")" build/neon_link.bin

echo "Waiting for app_main after reset..."
if python3 - "$PORT" <<'PY'
import sys, time
try:
    import serial
except ImportError:
    print("pyserial not available; skip boot check", file=sys.stderr)
    sys.exit(0)

port = sys.argv[1]
# USB Serial/JTAG ignores baud; 115200 is the IDF default.
ser = serial.Serial(port, 115200, timeout=0.25)
deadline = time.time() + 20
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
        "Press RST only (not BOOT). If the OLED is still two pixels, "
        "the image did not start.",
        file=sys.stderr,
    )
    sys.exit(1)
print("BOOT CHECK OK")
PY
then
  echo "Done. Monitor with: idf.py -p $PORT monitor"
else
  echo "Flash wrote, but the app did not announce itself." >&2
  echo "Press RST once (not BOOT) and watch the OLED." >&2
  exit 1
fi
