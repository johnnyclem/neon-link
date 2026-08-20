#!/usr/bin/env bash
# Flash link-sync to M5Stack Tab5 (ESP32-P4 USB JTAG/serial).
# Isolated build dir — will not overwrite S3 / CrowPanel sdkconfig.
#
# Download: hold RESET ~2 s until the green LED flashes, then run this.
# USB-C native JTAG often auto-resets without that.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

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
  echo "No USB JTAG/serial (cu.usbmodem*). Plug in the Tab5 USB-C." >&2
  exit 1
fi

BUILD_DIR="build-linksync-tab5"
SDKCONFIG="$BUILD_DIR/sdkconfig"
DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.linksync-tab5"

echo "Building link-sync-tab5 (ESP32-P4 MIPI, isolated $BUILD_DIR)..."
idf.py -B "$BUILD_DIR" -DSDKCONFIG="$SDKCONFIG" \
  -DSDKCONFIG_DEFAULTS="$DEFAULTS" \
  set-target esp32p4
idf.py -B "$BUILD_DIR" -DSDKCONFIG="$SDKCONFIG" \
  -DSDKCONFIG_DEFAULTS="$DEFAULTS" \
  build

echo "Flashing to $PORT ..."
(
  cd "$BUILD_DIR"
  python -m esptool --chip esp32p4 -p "$PORT" -b 460800 \
    --before default_reset --after hard_reset \
    write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB \
    @flash_args
)

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
deadline = time.time() + 35
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
        "BOOT CHECK FAILED: no app_main breadcrumb. Hold RESET 2s for download, then retry.",
        file=sys.stderr,
    )
    sys.exit(1)
print("BOOT CHECK OK")
PY
then
  echo "Done. SoftAP is LINK-TAB-XXXX (password on the glass)."
  echo "Monitor:  python3 -m serial.tools.miniterm $PORT 115200"
else
  echo "Flash wrote, but the app did not announce itself." >&2
  exit 1
fi
