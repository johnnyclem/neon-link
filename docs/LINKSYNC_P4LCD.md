# link-sync — CrowPanel Advance 5.0" ESP32-P4

Ableton Link → MIDI-clock, same firmware path as
[link-epd](LINKSYNC_EPD.md), on the **Elecrow CrowPanel Advance 5.0"**
(ESP32-P4NRW32 + ESP32-C6, 800×480 IPS). The glass is RGB, so it can
tick. SoftAP is the provision path (no on-chip BLE).

## Hardware

| | |
|---|---|
| SoC | ESP32-P4NRW32 **rev v1.3**, 16 MB flash, 32 MB HEX PSRAM |
| Radio | XIAO ESP32-C5 over UART1 (ESP-Hosted 2.12, dual-band). Onboard C6 unused. |
| Panel | 800×480 RGB565, ~25 MHz PCLK, double-buffered scanout |
| Console | CH343 on the UART USB-C (`/dev/cu.wchusbserial*`) |
| Touch | GT911 on I2C 45/46, RST GPIO 36, INT GPIO 42 |
| Backlight | STC8H1K28 @ `0x2F` PWM |
| Speakers | 2×3 W on dual NS4168 I2S amps — LRCK 21, BCLK 22, SDOUT 23 |

C6 SDIO is **not** the Waveshare Function-EV map:

| Signal | GPIO |
|--------|------|
| CLK | 53 |
| CMD | 54 |
| D0..D3 | 52, 51, 50, 49 |
| RESET | 20 (active high) |

RGB data/sync eat GPIO 2–19, 40, 41. Do not enable the P4 EMAC — those
pins collide with SDIO.

## UART1 Crowtail — M5 Unit MIDI (SAM2695)

DIP on the back: **UART**, not wireless-module. The onboard C6 (Wi-Fi)
is SDIO and is unaffected.

| Net | GPIO | Grove wire (host names) |
|-----|------|-------------------------|
| UART1 TX | **47** | white (host TX → unit RX → SAM2695 + MIDI OUT) |
| UART1 RX | **48** | yellow (host RX ← MIDI IN after the opto) |
| 5V | — | red |
| GND | — | black |

Switch on the unit: **Separate (ON)** so IN and OUT are not tied.
Headphones on the 3.5 mm **audio** jack hear the SAM2695. Link clock
leaves UART1 TX onto MIDI OUT (DIN and TRS-A). MIDI IN is already
opto-isolated on the unit.

If OUT is silent and IN works (or the reverse), swap 47/48 — Crowtail
yellow/white vs M5 Grove is the usual mix-up.

Do not use UART3-IN for this. That port is 5V/2A power + IO27/28.

## Build & flash

Device in download: hold **BOOT**, tap **RESET**, release BOOT.

```bash
. ~/esp/esp-idf-v5.5.5/export.sh
./scripts/flash_linksync-p4lcd.sh /dev/cu.wchusbserial210
```

Isolated tree: `build-linksync-p4lcd/` + `sdkconfig.linksync-p4lcd`.
Does not overwrite the S3 / e-paper `sdkconfig`.

The **800×480 panel** defaults to **portrait** (480×800 logical, rotated
into the native scanout). **SYSTEM > SCREEN** switches to landscape.
No encoders — drive it from the glass. The live face is full-width:
integer BPM (tap-tempo), − / +, RUN / STOP, and a thin phase bar.
Tenths of a BPM are not shown. The old right-hand beat stage is gone.

| Hit | Action |
|-----|--------|
| BPM digits | tap-tempo |
| **−** / **+** | nudge ±1 BPM (hold to repeat) |
| **RUN** / **STOP** | transport toggle |
| Gear (top right) | settings — same sections as the web editor |

## Speakers — metronome click

The panel's two 3 W speakers hang off dual NS4168 I2S amps (LRCK 21,
BCLK 22, SDOUT 23 — no MCLK, no codec registers). The build carries the
audio engine; the click is opt-in from the glass or the web editor:
**AUDIO > CLICK** cycles **OFF / CLICK / WOOD / METRO** (default OFF).
LEVEL sets the volume. The accent always lands on the downbeat, and
the click follows the transport — speakers tick only while the panel
shows a running clock. Selecting a voice starts the audio engine;
AUDIO remains the master switch.

Settings tabs match the web UI: **OUTPUTS**, **NETWORK**, **MIDI**,
**AUDIO**, **SYSTEM**. Tap a row to cycle/toggle; **−** / **+** on a
row nudge the value (hold to repeat). Flick the list to scroll.
**NETWORK > SCAN** lists nearby 2.4 / 5 GHz networks; pick one, type the
password on the on-screen keyboard, **JOIN**. Open networks skip the
keyboard. **SYSTEM > COLOUR** restyles the live accent (Void graphite
chassis). **SYSTEM > REBOOT** asks first. Changes apply live and
persist like the OLED menu.

GT911 is on I2C 45/46 (RST 36, INT 42). Address is `0x5D` or `0x14`
depending on INT during reset. First-boot log should show
`touch GT911 @0x.. ok` and `live panel 800x480 touch=1`.

Link runs a local session. Crowtail MIDI is off while DIP=WM (UART1
is the C5 radio). SoftAP is `LINK-LCD-XXXX` (password on the glass /
`http://192.168.4.1`). `esp_wifi_init` is what brings the C5 up over
UART; do not also init the C6. Create `WIFI_AP_DEF` *before* that init
or lwIP panics (`netif already added`) and the panel reboots every ~20 s.

The Wi-Fi radio is a **XIAO ESP32-C5** in the expansion header
(ESP-Hosted UART, 2.4 + 5 GHz). DIP must be **WM**. UART1 GPIO 47/48
is that bus (P4 TX=48 → C5 D7/RX=GPIO12, P4 RX=47 ← C5 D6/TX=GPIO11,
921600 8N1; 47/48 are swapped vs the UART1 silk because the XIAO
socket is wired name-to-name). Crowtail MIDI on those same pins is
off. The onboard C6
is 2.4 GHz only and is not initialised.

Flash the C5 over its own USB-C (`/dev/cu.usbmodem*`), never the
CrowPanel CH343:

```bash
. ~/esp/esp-idf-v5.5.5/export.sh
cd tools/xiao-c5-hosted-uart
idf.py set-target esp32c5
idf.py -p /dev/cu.usbmodem2101 flash
```

XIAO `EN` is not on the 14-pin header, so the P4 cannot reset the C5.
If SoftAP does not come up, tap **RST** on the XIAO after the panel
boots. DIP **UART** routes 47/48 back to Crowtail and the C5 is then
only powered — no radio.

This unit: ESP32-P4 **rev v1.3**, MAC `e8:f6:0a:e0:44:a3`, 32 MB HEX
PSRAM @ 200 MHz, IDF 5.5.5. Do not flash a `REV_MIN_301` image.

## Idle dimming

With `Idle dim after` set (web editor System page or SYSTEM > DIM on
the panel; default off), the backlight dims through the STC8 expander
PWM after that many seconds without a touch; a stopped transport
blanks entirely after three dim windows (a playing one only dims).
While blank, composition is skipped and the waking tap is swallowed so
it cannot press a control blind. `neon::ui::IdleDimmer`, host-tested.
