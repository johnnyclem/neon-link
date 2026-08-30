# link-sync — Waveshare ESP32-S3-RLCD-4.2 (ST7305 reflective LCD)

An always-legible, battery-friendly Ableton Link peer. Same repo, same
GPLv2+ license, same MIDI-clock path as the XIAO dongle
([docs/LINKSYNC.md](LINKSYNC.md)). The 4.2" reflective panel is the
status surface; the KEY and BOOT buttons are the whole front panel.
There are three buttons across the top edge — KEY, BOOT, and PWR in
the middle — but only KEY and BOOT are user-readable GPIOs; PWR is the
board's power on/off button (single click on, long press off).

**Reference board:** Waveshare **ESP32-S3-RLCD-4.2**
(ESP32-S3-WROOM-1-N16R8 — 16 MB flash, 8 MB octal PSRAM, native
USB-C). 4.2" ST7305 reflective mono LCD, 400×300 landscape, sunlight
readable, no backlight. Onboard: PCF85063 RTC + SHTC3 (I2C), microSD
(1-bit SDMMC), ES8311/ES7210 audio codecs, LiPo charger with a ÷3
VBAT divider on GPIO4, a 2×8 expansion header.

Board reference: <https://docs.waveshare.com/ESP32-S3-RLCD-4.2>

```
┌──────────────────────────────────┐
│ link-rlcd        PEERS 2   [▮▮ ]│
│                                  │
│ 128.3  BPM                       │
│                                  │
│ ■ □ □ □        beat, live        │
│ PLAYING                          │
│ studio-wifi                      │
│ MIDI CLOCK  24 PPQN  TRS-A       │
└──────────────────────────────────┘
   KEY tap = play/stop   KEY hold = menu
   BOOT tap = +1 BPM     BOOT hold = tempo screen
```

Tapping BOOT trims the tempo up by one. To go the other way — or to
move fast — **hold BOOT** to open the Tempo screen: there BOOT is up,
KEY is down, and holding either one auto-repeats (accelerating). The
screen closes itself a few seconds after the last press. There is no
dedicated "down" button because the third top button is the PWR
button (power on/off), leaving only KEY and BOOT for control.

## Why a reflective LCD

The e-paper face ([LINKSYNC_EPD.md](LINKSYNC_EPD.md)) had to ration
its repaints: full refreshes flash, partials ghost, and the planner's
design rule was *do not treat the glass as a metronome*. The ST7305 is
the opposite trade: it scans continuously like any LCD (no flash, no
ghosting, sub-frame SPI updates) while holding an e-paper-class power
budget, because the controller has two scan modes:

- **HPM** (`0x38`): 32 Hz scan — live interaction, beat display.
- **LPM** (`0x39`): 1 Hz scan, tens of µA — the idle hold. The image
  stays on the glass, even while the ESP32 deep-sleeps.

Switching is a single command byte, so the firmware rides HPM while
anything moves and falls back to LPM three seconds after the last
change (`neon::RlcdFramePlanner`, host-tested). Net effect: this is
the one link-sync face that shows the beat *as it happens* and still
idles at microamps.

## What runs

Everything the other S3 dongles run: Ableton Link (WiFi), 24 PPQN TRS
MIDI clock + transport + SPP from the core-1 engine, MIDI clock PLL
follow on MIDI IN, BLE provisioning, SoftAP + web editor, OTA with
rollback. Plus, unique to this face:

- **Live beat dots** — one box per quantum beat, filled on the beat.
- **Battery gauge** — VBAT through the ÷3 divider on GPIO4, coarse
  LiPo curve, quantized so ADC jitter never repaints the glass.
- **Two-button front panel** (`neon::RlcdFrontPanel`, host-tested):
  - Live: KEY tap = play/stop, KEY hold = settings, BOOT tap = +1 BPM,
    BOOT hold = Tempo screen.
  - Tempo: BOOT = up, KEY = down, hold either to auto-repeat
    (accelerating). Self-closes after a few idle seconds. This is the
    home for both directions, since only two front buttons are usable
    (the third is the PWR button, reserved for power on/off).
  - Menu: BOOT tap/hold = cursor down/up, KEY tap = edit, KEY hold =
    back. Same six settings as the e-paper face plus a POWER row
    (restart / power off / cancel).
  - Power off paints the splash, drops the panel to LPM (the image
    persists), and deep-sleeps the ESP32. KEY wakes it.

- **Landscape / portrait** — the panel is a 300×400 portrait controller
  the firmware normally drives as 400×300 landscape. `config.display_portrait`
  picks a genuine portrait layout (300×400, the status face reflowed
  tall) for a portrait stand. There is no accelerometer, so it is a
  stored preference toggled two ways:
  - **Blind chord:** hold **KEY + BOOT together for 3 s**. After ~0.7 s
    a "ROTATING TO …" countdown appears in the orientation it is about
    to switch to (so it reads upright in the stand you are turning
    toward); release early to cancel. Chosen over a menu item because a
    wrong-orientation screen is unreadable, but the chord is not — and
    over the PWR button, which is not a readable GPIO. The individual
    KEY/BOOT gestures are suppressed for the duration, so the flip never
    also opens the menu or Tempo screen.
  - **Web editor:** the `display_portrait` field, for a non-blind path.
  The rotation lives entirely in `RlcdCanvas` (host-tested): portrait
  draw ops take logical 300×400 coordinates that map into the physical
  400×300 buffer, so the packer and ST7305 driver are untouched.

## Pin map

From the vendor board manifest and schematic
(`CONFIG_NEON_BOARD_LINKSYNC_RLCD` branch of `board_pins.h`):

| Function | GPIO |
|---|---|
| ST7305 SCK / MOSI | 11 / 12 |
| ST7305 CS / DC / RST | 40 / 5 / 41 |
| ST7305 TE (unused) | 6 |
| I2C SDA / SCL (RTC, SHTC3, codecs) | 13 / 14 |
| TRS MIDI TX / RX (UART1 @ 31250) | 43 / 44 |
| Battery ADC (÷3) | 4 |
| KEY / BOOT (top edge, active-low) | 18 / 0 |
| I2S MCLK / BCLK / WS / DOUT / DIN | 16 / 9 / 45 / 8 / 10 |
| Speaker amp enable | 46 |
| SDMMC CLK / CMD / D0 (unused) | 38 / 21 / 39 |

The console is the native USB Serial/JTAG on the Type-C port, so the
classic U0TXD/U0RXD pads (GPIO 43/44) on the 2×8 expansion header are
free and carry TRS MIDI, exactly like the MaTouch target. Type A TRS:
tip = current source through 220 Ω, ring = GND; opto (6N138) on RX.

The ES8311/ES7210 audio path is wired into the Kconfig I2S defaults
but `CONFIG_NEON_AUDIO` ships **off** — enable it manually if you want
the metronome click through the onboard speaker, and measure the heap
first (see the Kconfig help on `NEON_AUDIO_INPUT`).

## Driver notes (halesp::rlcd_st7305)

- Init order, register table, and the 120 ms post-`SLPOUT` delay
  follow the vendor firmware for this glass; the values are
  load-bearing.
- The controller packs **12 native columns per 3-byte group** and
  2 native rows per byte; CASET is written mirrored (`0x3C - addr`,
  window `0x12..0x2A`). The packing lives in portable code
  (`neon::rlcd::pack_frame`) and is host-tested bit-for-bit against a
  transcription of the vendor's u8g2 path, so the ESP driver only
  moves bytes.
- The UI is landscape 400×300; the controller is portrait 300×400.
  Controller row *r* maps to landscape columns `x = 2r, 2r+1`, so a
  BPM digit change dirties a narrow row span. The driver diffs each
  present against a shadow frame and streams only dirty spans — a
  cursor move costs a few hundred SPI bytes at 24 MHz.
- `INVON` (0x21) is part of init, matching the vendor default: data 1
  = reflective/white, which lines up with the canvas polarity
  (1 = white, ink = 0) shared with the e-paper face.

## Build and flash

```bash
idf.py set-target esp32s3
idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.linksync-rlcd" build
./scripts/flash_linksync-rlcd.sh          # isolated build dir, both OTA slots
```

16 MB layout (`partitions_16mb.csv`): 6 MB OTA slots at `0x20000` /
`0x620000`. The flash script asserts the offsets before writing.

First boot: BLE provisioning (Espressif *ESP BLE Prov* app) or wait
for the SoftAP — the AP name and password are printed on the glass,
editor at `http://192.168.4.1`.

## Host tests

The portable pieces — canvas + panel layout, ST7305 packing, the
two-button state machine, the paint/power planner — run in the host
suite:

```bash
cmake -S host -B build-host -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host -j && ctest --test-dir build-host --output-on-failure
```

See `host/tests/test_rlcd_pack.cpp`, `test_rlcd_front.cpp`,
`test_rlcd_refresh.cpp`.
