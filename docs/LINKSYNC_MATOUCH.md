# link-sync — MaTouch ESP32-S3 1.28" Rotary (GC9A01)

A round-dial Ableton Link peer. Same repo, same GPLv2+ license, same
MIDI-clock path as the XIAO dongle ([docs/LINKSYNC.md](LINKSYNC.md)). The
240×240 round IPS is the status surface and the bezel encoder is the
control.

**Reference board:** Makerfabs **MaTouch ESP32-S3 1.28" Rotary**
(GC9A01). ESP32-S3-N16R8 — 16 MB flash, 8 MB octal PSRAM, native USB-C.
1.28" 240×240 round IPS on SPI, a quadrature rotary encoder around the
bezel, a CST816 cap-touch layer, haptic motor, RTC and microSD.

Makerfabs source / pin reference:
<https://github.com/Makerfabs/MaTouch-ESP32-S3-RotaryIPS-Display1.28-GC9A01>

```
        ╭───────────╮
        │   120.0   │   round GC9A01, 240×240
        │    BPM     │   phase ring sweeps once per bar
        ╰───────────╯
          twist = ±1 BPM
          click = play/stop   hold = tap tempo
```

## Why this board

The S3-N16R8 has the RAM and flash headroom the C3 stamp lacks: a full
RGB565 framebuffer (240×240×2 = 115 KB) rides in PSRAM, Link + WiFi run
comfortably, and the round panel makes a legible standalone tempo dial.
This is a **proof-of-concept target** — display bring-up, live tempo/phase,
and encoder transport. Cap-touch, haptic, RTC and SD are wired on the
board but not yet driven by the firmware.

## Locked pin map

Pins are the Makerfabs **MaTouch-1.28-DevKit** `Controller` example map
(`pin_config.h` / `Hello_world.ino`). Verified against the vendor source,
not guessed. **Note:** the ToolSet/DevKit Controller pinout differs
entirely from the separate "RotaryIPS-Display1.28-GC9A01" SKU — do not
mix them.

| Function | GPIO | Notes |
|----------|------|-------|
| LCD SCLK | **42** | GC9A01 SPI clock (SPI2, 40 MHz) |
| LCD MOSI | **2** | SPI data |
| LCD MISO | −1 | not connected |
| LCD CS | **1** | |
| LCD DC | **46** | data/command |
| LCD RES | **21** | reset |
| LCD BLK | **45** | backlight, active **HIGH** |
| Encoder A / CLK | **48** | PCNT ×4 quadrature |
| Encoder B / DT | **47** | |
| Encoder push | **17** | active-low; short = play/stop, long = tap |
| Haptic motor | 41 | not driven by the POC |
| Touch SDA | 38 | CST816 — not driven by the POC |
| Touch SCL | 39 | |
| Touch INT | 40 | |
| Touch RST | 18 | |

Pulse channels are virtual — there are no Eurorack jacks. Native USB
Serial/JTAG is the console; there is no USB-UART bridge.

## What it does

- **Boot splash** — "NEON / link-mat" proves the panel before Link is up.
- **Live face** — hero tempo (`120.0`), a 120-dot phase ring that fills
  once per bar with a bright head at the current beat position, peer
  count (`N LINK` + pips), and a RUN/STOP chip.
- **Encoder** — twist nudges tempo ±1 BPM per detent (`kNudgeTempo`),
  a short click toggles transport (`kToggle`), a long press taps tempo
  (`kTapTempo`). All go through the shared `control_queue`.
- First boot has no WiFi; the dial still runs on the internal timeline.
  Provision over SoftAP like the other link-sync boards.

## Display driver

GC9A01 via the `espressif/esp_lcd_gc9a01` managed component over the
esp_lcd SPI panel API (`components/neon_hal_esp/src/lcd_gc9a01.cpp`).
Color is inverted (`esp_lcd_panel_invert_color(true)`), element order
BGR, and the framebuffer stores RGB565 byte-swapped so pixels clock out
in GC9A01 wire order. The face renderer
(`main/matouch_service.cpp`) reuses the shared 5×7 font and the neon-tube
palette from the CrowPanel/Tab5 faces.

## Build & flash

The board enumerates as native USB Serial/JTAG (`/dev/cu.usbmodem*`). If
auto-reset into the ROM bootloader fails: hold **BOOT**, tap **RESET**,
release **BOOT**, re-run.

```
./scripts/flash_linksync-matouch.sh            # auto-detects the port
./scripts/flash_linksync-matouch.sh /dev/cu.usbmodem101
```

Isolated `build-linksync-matouch/` build dir and `sdkconfig` — this does
not clobber another S3 tree. 16 MB OTA layout (`partitions_16mb.csv`):
ota_0 @ 0x20000, ota_1 @ 0x620000.

Monitor:

```
idf.py -B build-linksync-matouch -p /dev/cu.usbmodem101 monitor
```
