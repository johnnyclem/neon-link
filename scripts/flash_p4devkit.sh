#!/usr/bin/env bash
# Flash NEON LINK to a Waveshare ESP32-P4-Module-DEV-KIT over USB-C.
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
  echo "No /dev/cu.usbmodem* found. Plug in the P4-Module-DEV-KIT USB-C." >&2
  exit 1
fi

P4_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.p4devkit"
need_reconfigure=0
if [[ ! -f sdkconfig ]]; then
  need_reconfigure=1
elif ! grep -q 'CONFIG_IDF_TARGET="esp32p4"' sdkconfig; then
  echo "sdkconfig is not an esp32p4 tree; reconfiguring..."
  rm -f sdkconfig sdkconfig.old
  need_reconfigure=1
elif ! grep -q 'CONFIG_NEON_BOARD_P4DEVKIT=y' sdkconfig; then
  echo "sdkconfig is not the P4-DEV-KIT overlay; reconfiguring..."
  rm -f sdkconfig sdkconfig.old
  need_reconfigure=1
fi

if [[ "$need_reconfigure" -eq 1 ]]; then
  idf.py -DSDKCONFIG_DEFAULTS="$P4_DEFAULTS" set-target esp32p4
fi

echo "Building P4-Module-DEV-KIT firmware..."
idf.py -DSDKCONFIG_DEFAULTS="$P4_DEFAULTS" build

echo "Flashing to $PORT ..."
idf.py -p "$PORT" -DSDKCONFIG_DEFAULTS="$P4_DEFAULTS" flash

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
        "BOOT CHECK FAILED: no app_main breadcrumb. Press RST only (not BOOT).",
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
