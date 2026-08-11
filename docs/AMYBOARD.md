# NEON LINK on AMYboard

Primary v1 target (FEATURES.md, 2026-08-09): the shorepine **AMYboard**
10HP Eurorack module (ESP32-S3-WROOM-1, Eurorack power, 2× ±10 V CV I/O,
TRS MIDI, front-panel Grove I2C).

## I/O map

| NEON LINK function | AMYboard hardware | Notes |
|--------------------|-------------------|-------|
| Tempo CV           | CV out 1 (GP8413 ch0) | 0–5 V over configured BPM range |
| Primary clock/gate | CV out 2 (GP8413 ch1) | 0 V / 5 V from CLK1 edges |
| Clock In           | CV in 1 (ADS1015 ch0) | Rising edge ≥ 1 V |
| Reset In           | CV in 2 (ADS1015 ch1) | Rising edge ≥ 1 V |
| TRS MIDI out       | MIDI OUT (GPIO 14, Type A) | UART1 @ 31250 baud |
| OLED 128×128       | Front Grove I2C (SDA 17 / SCL 18) | Same as `amyboard.init_display()` |
| Encoder / LEDs     | — | Use web editor; I2C encoder later |
| Ethernet           | — | Not present; WiFi + setup AP only |

### Display support (128×128)

Probed at boot in this order (matches MicroPython `amyboard.init_display()`):

1. **SSD1327 @ 0x3d** — Adafruit 1.5" grayscale STEMMA QT
2. **SH1107 @ 0x3c** — generic 128×128 mono I2C modules
3. **SSD1306 @ 0x3c** — classic 128×64 (top half of the UI framebuffer)

**SPI SH1107:** set `kPinDispSck/Mosi/Cs/Dc` (and optional `Res`) in
`board_pins.h` — defaults are `-1` (disabled). Suggested AMYboard wiring:
SCK=12, MOSI=11, CS=10, DC=7 (MPIO_C0).

CLK2–4 / RESET / RUN exist as virtual channels in the engine (for BLE-MIDI
gate routing and future expansion) but only CLK1 is mirrored to a jack.

## Build & flash

Requires ESP-IDF **v5.3.2** (same as CI) and the Ableton Link submodule:

```bash
cd neon-link
git submodule update --init --recursive

# ESP-IDF
. ~/esp/esp-idf-v5.3.2/export.sh   # or your install path

idf.py set-target esp32s3
idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.amyboard" build

# Flash (if auto-reset fails: hold BOOT+RST, release RST then BOOT)
./scripts/flash_amyboard.sh
# or:
python -m esptool --chip esp32s3 -p /dev/cu.usbmodem* -b 115200 \
  --before default_reset --after no_reset write_flash \
  --flash_mode dio --flash_freq 80m --flash_size 16MB \
  0x0 build/bootloader/bootloader.bin \
  0x8000 build/partition_table/partition-table.bin \
  0x10000 build/neon_link.bin
# Then press RST once (not BOOT) to leave download mode and run the app.
```

**USB Serial note:** Opening the port can leave the chip in ROM download mode
(`boot:0x3 waiting for download`). Press **RST only** to boot the app, then
attach the monitor with DTR/RTS left deasserted.

**Warning:** flashing replaces the stock MicroPython AMYboard firmware.
To restore MicroPython later, reflash from [amyboard.com](https://amyboard.com)
or the rolling `amyboard` GitHub release.

### First boot without WiFi credentials

1. Power the module (USB-C or Eurorack +12 V).
2. After ~60 s with no network, it raises open AP `NEON-LINK-XXXX`.
3. Join the AP, open `http://192.168.4.1/`, enter home WiFi SSID/password.
4. Reboot; the module joins STA, serves the editor at `http://neon-link.local/`.

Or bake credentials at build time:

```
idf.py menuconfig   # NEON LINK configuration → WiFi SSID / password
```

## Timing notes

- Pulse *scheduling* is still GPTimer @ 1 µs on core 1.
- Jack edges go through the GP8413 over I2C (~100 µs write + 1 ms FreeRTOS
  poll on the mirror task), so gate jitter is on the order of a millisecond
  — fine for modular clocks, not for audio-rate.
- External clock following samples the ADS1015 at ~1 kHz; good for tempo
  tracking, not for sample-accurate phase lock.

## Success criteria (FEATURES.md)

- [x] Ableton Link over WiFi
- [x] Bidirectional: CV in 1 → Link tempo
- [x] Tempo CV on CV out 1
- [x] Primary clock/gate on CV out 2
- [x] BLE MIDI (disableable) + TRS MIDI out
- [x] OLED if attached; web UI always
- [x] Eurorack power + 10HP (AMYboard)
