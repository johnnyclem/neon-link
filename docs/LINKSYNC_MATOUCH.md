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
| Touch SDA | 38 | CST816 cap-touch (drives the settings panel) |
| Touch SCL | 39 | |
| Touch INT | 40 | |
| Touch RST | 18 | |
| MIDI TX | **43** | header pin 15, U0TXD, UART1 @ 31250 |
| MIDI RX | **44** | header pin 14, U0RXD, opto required |

Pulse channels are virtual. Native USB Serial/JTAG is the console, so
classic UART0 on GPIO 43/44 is free. MIDI is the **right-hand** 24-pin
column, skipping the 5 V rail on the top pin:

| Header pin | Net | GPIO |
|------------|-----|------|
| 12 | 3V3 | — (MIDI-chip VCC, jumpers 3.3 V) |
| 13 | GND | — |
| 14 | RX | **44** |
| 15 | TX | **43** |

Do not use the 5 V pin. Swap 43/44 if OUT is silent. Clock/transport
follow is the C3 OLED PLL path (`midi_service`). CLK OUT still emits
Link 24 PPQN while playing.

## What it does

- **Boot splash** — "NEON / link-mat" proves the panel before Link is up.
- **Live face** — hero tempo (`120`), a 120-dot phase ring that fills
  once per bar with a bright head at the current beat position, peer
  count (`N LINK` + pips), and a RUN/STOP chip. Colour palettes (Teal
  default, Void, Phosphor, Amber, Magenta, Paper) live under
  Settings → System → COLOUR; the same choice is on the web editor.
- **Beat styles** — while playing with Settings → System → BEAT on, the
  dial shows a full-screen beat animation set by STYLE: Number, Pie,
  Pendulum or Pulse (a colour echo of `neon::ui::draw_beat_stage`). BEAT
  off keeps the glanceable hero BPM.
- **Encoder** — twist nudges tempo ±1 BPM per detent (`kNudgeTempo`),
  a short click toggles transport (`kToggle`), a long press taps tempo
  (`kTapTempo`). All go through the shared `control_queue`. In settings,
  twist moves the row, click activates, long-press goes back.
- **Touch settings** — tap the gear to open. Drag to scroll the list; on a
  value row, tap the left half to decrement and the right half to
  increment. The menu is trimmed to what this board can do (no OUTPUTS or
  AUDIO section; MIDI shows CLK OUT; SYSTEM drops the pulse-timing rows).
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

## Brightness and idle dimming

The BRIGHT row (and the web editor's Display brightness) is now real on
this board: the backlight runs on 20 kHz LEDC PWM instead of a bare
GPIO, so 0-255 is 0-255, not on/off. With `Idle dim after` set (web
editor or the SYSTEM > DIM row; default off), the panel dims to the
configured level after that many seconds without a touch or encoder
input, and a *stopped* transport blanks entirely after three dim
windows — a playing one only dims, the dial stays glanceable. While
blank the 115 KB SPI blit is skipped and the waking touch or detent is
swallowed (it turns the glass back on; it does not act on whatever was
under it). `neon::ui::IdleDimmer`, host-tested.
