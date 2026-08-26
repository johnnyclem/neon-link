# NEON LINK on AMYboard

Primary v1 target (FEATURES.md, 2026-08-09): the shorepine **AMYboard**
10HP Eurorack module (ESP32-S3-WROOM-1, Eurorack power, 2× ±10 V CV I/O,
TRS MIDI, front-panel Grove I2C).

## I/O map

| NEON LINK function | AMYboard hardware | Notes |
|--------------------|-------------------|-------|
| Tempo CV           | CV out 1 (GP8413 ch0) | 0–5 V over configured BPM range |
| Primary clock/gate | CV out 2 (GP8413 ch1) | 0 V / 5 V from CLK1 edges |
| Clock In           | CV in 1 (ADS1015 ch0) | Rising ≥ 2.5 V, falling ≤ 1.0 V (idle jacks sit ~1.2 V) |
| Reset In           | CV in 2 (ADS1015 ch1) | Same hysteresis |
| TRS MIDI out       | MIDI OUT (GPIO 14, Type A) | UART1 @ 31250 baud |
| OLED 128×128       | Front Grove I2C (SDA 17 / SCL 18) | Same as `amyboard.init_display()` |
| Encoder / LEDs     | Grove I2C on a hub with the OLED | **NULLLAB expander @ 0x24:** E1/E2/E3 = KY-040 (CLK/DT/SW); E0 = 10k B pot → BPM when enabled. **M5Stack Unit Encoder (U135) @ 0x40:** I2C pulse count + click; both SK6812s light dim magenta when the firmware finds it. A passive 3-into-1 Grove hub is the right splitter (shared I2C, unique addresses). A GPIO-only Grove encoder cannot share the hub with the OLED. |
| Ethernet           | — | Not present; WiFi + setup AP only |
| S/PDIF in / out    | PCM9211 (`RXIN0` / `MPO0`) | **AC-coupled, not GPIO.** 100 nF series on both tips. Not usable as clock / gate / CV — leave labeled SPDIF (`docs/SPDIF_BENCH_TEST.md`) |

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
# That script is the only supported USB flash path. It forces the 16 MB
# AMYboard table and writes the app to both OTA slots. `idf.py flash`
# used to program only ota_0; a reboot then rolled back onto empty
# ota_1 — blank OLED, two pixels lit. Do not flash the app at 0x10000
# (that is the old factory offset; this table's ota_0 is 0x20000).
# Then press RST once (not BOOT) if the panel stays dark after the
# script's boot check.
```

**USB Serial note:** Opening the port can leave the chip in ROM download mode
(`boot:0x3 waiting for download`). Press **RST only** to boot the app, then
attach the monitor with DTR/RTS left deasserted.

**Warning:** flashing replaces the stock MicroPython AMYboard firmware.
To restore MicroPython later, reflash from [amyboard.com](https://amyboard.com)
or the rolling `amyboard` GitHub release.

### First boot without WiFi credentials

1. Power the module (USB-C or Eurorack +12 V).
2. After ~10 s with no credentials (or ~60 s if credentials fail), it raises
   the WPA2 setup AP `NEON-LINK-XXXX`. The password is generated once per
   unit from its own MAC address (not a shared default printed in a doc —
   every unit in a batch gets a different key) and is shown on the OLED's
   **NETWORK** screen for as long as the setup AP stays up; change it on
   the web UI's Network page afterward if you like. An open AP can be
   enabled there too, but it exposes the whole HTTP API, OTA included, to
   anyone in radio range.
3. Join the AP, open `http://192.168.4.1/` (also shown on the OLED).
4. Enter home WiFi SSID/password → **SAVE** (STA joins live; **REBOOT**
   only if association sticks).
5. Rejoin your home network; open **`http://neon-link.local/`**
   (mDNS hostname + `_http._tcp`). If `.local` fails on your OS, use the
   IP shown on the OLED / serial log (`got IP a.b.c.d`).

Or bake credentials at build time:

```
idf.py menuconfig   # NEON LINK configuration → WiFi SSID / password
```

**API extras:** `GET /api/status` includes `hostname`, `ip`, `setup_ap`;
`POST /api/reboot` soft-resets so WiFi changes take effect.

### Updating firmware

Join the module's network (the setup AP, or your home WiFi if it has
already joined one), open its web page, and go to **System → Module**. Pick
the new `.bin` file under **Firmware** and press **Install update** — the
page uploads it, the module writes it to the spare flash slot, verifies it,
and reboots into it on its own, in under a minute. Nothing else to do; if
the new image turns out to be bad, the bootloader notices it never finished
starting up cleanly and automatically falls back to the version that was
running before, so there is no way to end up with a bricked unit from a bad
update. The OLED's **SYSTEM** screen always shows which version is
currently running (**VERSION** row), which is the first thing worth
checking when reporting an issue.

## Timing notes

- Pulse *scheduling* is still GPTimer @ 1 µs on core 1.
- Jack edges go through the GP8413 over I2C (~100 µs write + 1 ms FreeRTOS
  poll on the mirror task), so gate jitter is on the order of a millisecond
  — fine for modular clocks, not for audio-rate.
- External clock following samples the ADS1015 at ~1 kHz; good for tempo
  tracking, not for sample-accurate phase lock.
- The audio path is the exception: with `CONFIG_NEON_AUDIO`, a jack
  carrying a clock/reset/run role places its edges to within one sample
  (~21 µs at 48 kHz), because they are rendered from the same
  `MultiClockEngine` against a `SampleClock`-derived timebase rather than
  poked out over I2C. See `docs/AUDIOLINK.md`.

## Audio (docs/AUDIOLINK.md)

The LINE jack is a PCM3060 (I2S slave, 32-bit left-justified slots, 256fs
MCLK) on the tulip/amyboard pin map: MCLK=3, BCLK=8, WS=2, DOUT=6, DIN=9.
Those are the AMYBOARD Kconfig defaults. TX-only by default — a duplex
DMA ring ate ~64 kB of internal RAM and the chip rebooted when WiFi
associated.

- 48 kHz stereo, 256-frame blocks, 8 DMA descriptors (~5.3 ms render
  period, ~42 ms of DMA slack so a late write does not auto-clear).
- The render task runs on core 1 at `configMAX_PRIORITIES - 4`, below the
  pulse task and the CV mirror.
- Link's asio service task runs at priority 8 (priority 2 lost to
  HTTP/OLED and the play ring bled ~40 ms/s). SoftAP (192.168.4.0/24) is
  hidden from Link discovery whenever STA has a real LAN address, so Live
  can reach the unicast audio port.
- 16 MB partition table (`partitions_16mb.csv`, 6 MB OTA slots).

## Success criteria (FEATURES.md)

- [x] Ableton Link over WiFi
- [x] Bidirectional: CV in 1 → Link tempo
- [x] Tempo CV on CV out 1
- [x] Primary clock/gate on CV out 2
- [x] BLE MIDI (disableable) + TRS MIDI out
- [x] OLED if attached; web UI always
- [x] Eurorack power + 10HP (AMYboard)
