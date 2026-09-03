#!/usr/bin/env bash
# Nearby spike: flash Neon Sync (no Ableton Link) to an ACEIRMC / Super Mini
# ESP32-C3 0.42" OLED stamp. Isolated build dir + sdkconfig so this does
# not clobber the working Link tree (build-linksync-c3oled / sdkconfig.linksync-c3oled).
#
# THIS OVERWRITES LINK FIRMWARE on the stamp. Use --build-only to compile
# and prove the ELF has no ableton:: / asio:: symbols without flashing.
#
# Port pick: Espressif USB-Serial/JTAG (VID 0x303A PID 0x1001). Never take
# the first /dev/cu.usbmodem* — a Teensy (MicroDexed) sorts ahead of the C3
# and esptool then dies with "Failed to connect to ESP32-C3: No serial data
# received."
#
# Super Mini C3 has no DTR/RTS auto-reset caps. If the chip is already in
# the ROM loader (hold BOOT, tap RESET, release BOOT):
#   --before no_reset  is the sequence that works
#   --after watchdog_reset leaves download mode (hard_reset via RTS does not)
#
# MIDI UART pins (UART1). This stamp's spacing crosses the silk labels;
# the default is TX=20 RX=21. Override per flash:
#   ./scripts/flash_linksync-c3oled-nsync.sh --tx-pin 20 --rx-pin 21
#   ./scripts/flash_linksync-c3oled-nsync.sh --build-only
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

list_c3_candidates() {
  python - <<'PY'
from serial.tools import list_ports

# Teensy, Apple hubs/displays, Realtek USB-LAN — never ESP32.
SKIP_VID = {0x16C0, 0x05AC, 0x0BDA}

prefer, fallback, ignored = [], [], []
for p in list_ports.comports():
    dev = p.device or ""
    if not dev.startswith("/dev/cu."):
        continue
    vid = p.vid or 0
    pid = p.pid or 0
    product = p.product or p.description or ""
    if vid in SKIP_VID:
        ignored.append("%s (%s vid=%04X pid=%04X)" % (dev, product, vid, pid))
        continue
    if vid == 0x303A and pid == 0x1001:
        prefer.append(dev)
    elif "usbmodem" in dev or "usbserial" in dev or "wchusbserial" in dev \
            or "SLAB_USBtoUART" in dev:
        fallback.append(dev)

for line in ignored:
    print("IGN %s" % line)
for dev in prefer + fallback:
    print("OK %s" % dev)
PY
}

PORT=""
MIDI_TX=20
MIDI_RX=21
BEFORE="no_reset"
BUILD_ONLY=0

usage() {
  cat <<'EOF'
Nearby spike: Neon Sync (no Ableton Link) on an ACEIRMC / Super Mini
ESP32-C3 0.42" OLED stamp.

Usage: flash_linksync-c3oled-nsync.sh [PORT] [--tx-pin N] [--rx-pin N] [--build-only]

  --tx-pin N     UART1 MIDI TX GPIO (default 20)
  --rx-pin N     UART1 MIDI RX GPIO (default 21)
  --build-only   Compile and check the ELF; do not flash

Isolated tree: build-linksync-c3oled-nsync / sdkconfig.linksync-c3oled-nsync.
Flashing overwrites the working Link firmware on the stamp.
EOF
}

require_gpio() {
  local name="$1" val="$2"
  if [[ ! "$val" =~ ^[0-9]+$ ]] || (( val < 0 || val > 21 )); then
    echo "$name must be an integer GPIO 0..21 (got '$val')" >&2
    exit 1
  fi
  case "$val" in
    11|12|13|14|15|16|17)
      echo "warning: GPIO$val is not bonded on ESP32-C3 Super Mini" >&2
      ;;
    18|19)
      echo "warning: GPIO$val is native USB D−/D+ on this stamp" >&2
      ;;
  esac
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --tx-pin)
      MIDI_TX="${2:?--tx-pin needs a GPIO number}"
      shift 2
      ;;
    --rx-pin)
      MIDI_RX="${2:?--rx-pin needs a GPIO number}"
      shift 2
      ;;
    --tx-pin=*)
      MIDI_TX="${1#*=}"
      shift
      ;;
    --rx-pin=*)
      MIDI_RX="${1#*=}"
      shift
      ;;
    --build-only)
      BUILD_ONLY=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    -*)
      echo "Unknown flag: $1" >&2
      usage >&2
      exit 1
      ;;
    *)
      if [[ -n "$PORT" ]]; then
        echo "Unexpected argument: $1" >&2
        usage >&2
        exit 1
      fi
      PORT="$1"
      shift
      ;;
  esac
done

require_gpio --tx-pin "$MIDI_TX"
require_gpio --rx-pin "$MIDI_RX"
if [[ "$MIDI_TX" == "$MIDI_RX" ]]; then
  echo "MIDI TX and RX cannot be the same GPIO ($MIDI_TX)" >&2
  exit 1
fi

candidates=()
if [[ "$BUILD_ONLY" -eq 0 ]]; then
  if [[ -z "$PORT" ]]; then
    echo "Scanning serial ports for Espressif USB-Serial/JTAG..."
    while IFS= read -r line; do
      case "$line" in
        IGN\ *)
          echo "  skip ${line#IGN }"
          ;;
        OK\ *)
          candidates+=("${line#OK }")
          echo "  candidate ${line#OK }"
          ;;
      esac
    done < <(list_c3_candidates)
    if [[ ${#candidates[@]} -eq 0 ]]; then
      echo "No Espressif USB-Serial/JTAG (0x303A:0x1001) found." >&2
      echo "Plug in the C3 USB-C. Other usbmodem devices (Teensy, etc.) are ignored." >&2
      exit 1
    fi
  else
    candidates=("$PORT")
  fi
fi

BUILD_DIR="build-linksync-c3oled-nsync"
SDKCONFIG="sdkconfig.linksync-c3oled-nsync"
DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.linksync-c3oled;sdkconfig.defaults.linksync-c3oled-nsync"

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
elif ! grep -q 'CONFIG_NEON_SYNC=y' "$SDKCONFIG"; then
  echo "sdkconfig is not the Neon Sync overlay; reconfiguring..."
  rm -f "$SDKCONFIG" "${SDKCONFIG}.old"
  need_reconfigure=1
fi

idf=(idf.py -B "$BUILD_DIR" -DSDKCONFIG="$SDKCONFIG" -DSDKCONFIG_DEFAULTS="$DEFAULTS")

if [[ "$need_reconfigure" -eq 1 ]]; then
  "${idf[@]}" set-target esp32c3
fi

python3 - "$SDKCONFIG" "$MIDI_TX" "$MIDI_RX" <<'PY'
import os, re, sys
path, tx, rx = sys.argv[1], sys.argv[2], sys.argv[3]
text = open(path).read() if os.path.exists(path) else ""

def set_key(s, key, val):
    line = "%s=%s" % (key, val)
    pat = re.compile(r"^#?\s*" + re.escape(key) + r"=.*$", re.M)
    if pat.search(s):
        return pat.sub(line, s, count=1)
    return s.rstrip() + "\n" + line + "\n"

text = set_key(text, "CONFIG_NEON_C3OLED_MIDI_TX", tx)
text = set_key(text, "CONFIG_NEON_C3OLED_MIDI_RX", rx)
open(path, "w").write(text)
PY
echo "MIDI UART1 TX=GPIO${MIDI_TX} RX=GPIO${MIDI_RX}"

echo "Building Nearby / Neon Sync C3 OLED firmware (no Ableton Link)..."
"${idf[@]}" build

if ! grep -q 'CONFIG_NEON_SYNC=y' "$SDKCONFIG"; then
  echo "Refusing to continue: $SDKCONFIG does not have CONFIG_NEON_SYNC=y" >&2
  exit 1
fi

echo "Checking ELF for Ableton Link / asio symbols..."
python3 - "$BUILD_DIR/neon_link.elf" <<'PY'
import subprocess, sys

elf = sys.argv[1]
nm = subprocess.run(
    ["riscv32-esp-elf-nm", "-C", elf],
    check=True, capture_output=True, text=True, errors="replace",
)
hits = []
for line in nm.stdout.splitlines():
    low = line.lower()
    if "ableton::" in low or "asio::" in low:
        hits.append(line)
if hits:
    sys.stderr.write("GPL check FAILED — Link/asio symbols in the image:\n")
    for h in hits[:40]:
        sys.stderr.write("  %s\n" % h)
    sys.exit(1)
print("GPL check OK — no ableton:: or asio:: symbols")
PY

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

if [[ "$BUILD_ONLY" -eq 1 ]]; then
  echo "Build-only: $BUILD_DIR/neon_link.bin (Neon Sync / Nearby). Not flashing."
  echo "This image overwrites Link firmware. Flash later with:"
  echo "  ./scripts/flash_linksync-c3oled-nsync.sh"
  exit 0
fi

echo "WARNING: flashing Neon Sync overwrites the working Ableton Link image on this stamp."

# Confirm an ESP32-C3 answers before writing. Prefer no_reset: the stamp is
# already in the ROM loader when the user held BOOT + RESET.
PORT=""
BEFORE=""
for before in no_reset usb_reset default_reset; do
  for cand in "${candidates[@]}"; do
    echo "Probing $cand --before $before ..."
    if python -m esptool --chip esp32c3 -p "$cand" -b 115200 \
         --before "$before" --after no_reset --connect-attempts 3 \
         chip_id; then
      PORT="$cand"
      BEFORE="$before"
      break 2
    fi
  done
done
if [[ -z "$PORT" ]]; then
  echo "No ESP32-C3 answered on: ${candidates[*]}" >&2
  echo "Hold BOOT, tap RESET, release BOOT, then re-run." >&2
  exit 1
fi

echo "Flashing to $PORT (ESP32-C3, --before $BEFORE) ..."
echo "  otadata @ $(printf '0x%x' "${OTADATA_OFF}")"
echo "  ota_0   @ $(printf '0x%x' "${OTA_0_OFF}")"
echo "  ota_1   @ $(printf '0x%x' "${OTA_1_OFF}")  (mirror — rollback must not hit erased flash)"

# Stub is already running from the probe (--after no_reset). Do not default_reset
# or the USB-JTAG session drops and we miss the ROM loader.
python -m esptool --chip esp32c3 -p "$PORT" -b 460800 \
  --before no_reset --after watchdog_reset \
  write_flash --flash_mode dio --flash_freq 80m --flash_size 4MB \
  0x0 "$BUILD_DIR/bootloader/bootloader.bin" \
  0x8000 "$BUILD_DIR/partition_table/partition-table.bin" \
  "$(printf '0x%x' "${OTADATA_OFF}")" "$BUILD_DIR/ota_data_initial.bin" \
  "$(printf '0x%x' "${OTA_0_OFF}")" "$BUILD_DIR/neon_link.bin" \
  "$(printf '0x%x' "${OTA_1_OFF}")" "$BUILD_DIR/neon_link.bin"

echo "Waiting for app_main after reset..."
if python - "$PORT" <<'PY'
import os, sys, time

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    print("pyserial not available; skip boot check", file=sys.stderr)
    sys.exit(0)

port = sys.argv[1]
ESP_VID, ESP_PID = 0x303A, 0x1001


def find_port(preferred):
    if preferred and os.path.exists(preferred):
        return preferred
    for p in list_ports.comports():
        if (p.device or "").startswith("/dev/cu.") and p.vid == ESP_VID and p.pid == ESP_PID:
            return p.device
    return None


deadline = time.time() + 8
found = None
while time.time() < deadline:
    found = find_port(port)
    if found:
        break
    time.sleep(0.2)
if not found:
    print("BOOT CHECK: USB-Serial/JTAG did not reappear after reset.", file=sys.stderr)
    sys.exit(1)
if found != port:
    print("USB re-enumerated as %s (was %s)" % (found, port), file=sys.stderr)
    port = found

ser = serial.Serial()
ser.port = port
ser.baudrate = 115200
ser.timeout = 0.25
ser.dsrdtr = False
ser.rtscts = False
ser.dtr = False
ser.rts = False
opened = False
open_deadline = time.time() + 5
while time.time() < open_deadline:
    try:
        ser.open()
        opened = True
        break
    except serial.SerialException:
        time.sleep(0.25)
if not opened:
    print("BOOT CHECK: could not open %s" % port, file=sys.stderr)
    sys.exit(1)
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
