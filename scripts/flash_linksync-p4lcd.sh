#!/usr/bin/env bash
# Flash link-sync to CrowPanel Advance 5.0" ESP32-P4 over the CH343 UART-C.
# Isolated build dir — will not overwrite the S3 / e-paper sdkconfig.
#
# Download: hold BOOT, tap RESET, release BOOT, then run this.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if [[ -z "${IDF_PATH:-}" ]]; then
  if [[ -f "$HOME/esp/esp-idf-v5.5.5/export.sh" ]]; then
    # shellcheck disable=SC1091
    source "$HOME/esp/esp-idf-v5.5.5/export.sh"
  else
    echo "ESP-IDF v5.5.5 not found. Install it next to v5.3.2 and source export.sh." >&2
    exit 1
  fi
fi

PORT="${1:-}"
if [[ -z "$PORT" ]]; then
  PORT="$(ls /dev/cu.wchusbserial* 2>/dev/null | head -1 || true)"
fi
if [[ -z "$PORT" ]]; then
  PORT="$(ls /dev/cu.usbserial* 2>/dev/null | head -1 || true)"
fi
if [[ -z "$PORT" ]]; then
  echo "No CH343 / USB-serial port. Plug in the CrowPanel UART USB-C." >&2
  exit 1
fi

BUILD_DIR="build-linksync-p4lcd"
SDKCONFIG="$BUILD_DIR/sdkconfig"
DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.linksync-p4lcd"

# Stale C5 Hosted UART sdkconfig claims UART1; Crowtail MIDI needs it.
if [[ -f "$SDKCONFIG" ]] && grep -q 'CONFIG_ESP_HOSTED_UART_HOST_INTERFACE=y' "$SDKCONFIG"; then
  echo "Dropping stale C5 Hosted UART sdkconfig so Crowtail MIDI can own UART1."
  rm -f "$SDKCONFIG"
fi

echo "Building link-sync-p4lcd (ESP32-P4, isolated $BUILD_DIR)..."
idf.py -B "$BUILD_DIR" -DSDKCONFIG="$SDKCONFIG" \
  -DSDKCONFIG_DEFAULTS="$DEFAULTS" \
  set-target esp32p4
idf.py -B "$BUILD_DIR" -DSDKCONFIG="$SDKCONFIG" \
  -DSDKCONFIG_DEFAULTS="$DEFAULTS" \
  build

echo "Flashing to $PORT ..."
# P4 bootloader is at 0x2000 (not 0x0). flash_args paths are relative
# to the build dir. CH343: hard_reset after write.
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
deadline = time.time() + 30
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
        "BOOT CHECK FAILED: no app_main breadcrumb. Press RESET only (not BOOT).",
        file=sys.stderr,
    )
    sys.exit(1)
print("BOOT CHECK OK")
PY
then
  echo "Done. SoftAP is LINK-LCD-XXXX (password on the glass)."
  echo "Monitor:  python3 -m serial.tools.miniterm $PORT 115200"
else
  echo "Flash wrote, but the app did not announce itself." >&2
  echo "Press RESET once (not BOOT) and watch the LCD." >&2
  exit 1
fi
