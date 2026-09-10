# link-sync — CrowPanel Advance 5.0" ESP32-P4

Ableton Link → MIDI-clock, same firmware path as
[link-epd](LINKSYNC_EPD.md), on the **Elecrow CrowPanel Advance 5.0"**
(ESP32-P4NRW32 + ESP32-C6, 800×480 IPS). The glass is RGB, so it can
tick. SoftAP is the provision path (no on-chip BLE).

## Hardware

| | |
|---|---|
| SoC | ESP32-P4NRW32 **rev v1.3**, 16 MB flash, 32 MB HEX PSRAM |
| Radio | Onboard ESP32-C6 over SDIO (ESP-Hosted 2.12, 2.4 GHz). XIAO C5 Hosted UART is abandoned. |
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

## UART1 Crowtail — MIDI IN+OUT

DIP on the back: **UART**, not wireless-module. That routes GPIO 47/48
to Crowtail. The onboard C6 (Wi-Fi) is SDIO and is unaffected.

| Net | GPIO | Grove wire (host names) |
|-----|------|-------------------------|
| UART1 TX | **47** | white (host TX → MIDI OUT) |
| UART1 RX | **48** | yellow (host RX ← MIDI IN after the opto) |
| 3V3 / 5V | — | red (MIDI-chip jumpers at 3.3 V) |
| GND | — | black |

Same four-wire hookup as the RLCD (TX, RX, 3V3, GND). Clock/transport
follow uses the C3 OLED PLL path (`midi_service`). Swap 47/48 if OUT
is silent.

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

Link runs a local session even if the C6 never INIT's. SoftAP is
`LINK-LCD-XXXX` (password on the glass / `http://192.168.4.1`). Create
`WIFI_AP_DEF` *before* `esp_wifi_init` or lwIP panics (`netif already
added`) and the panel reboots every ~20 s.

The Wi-Fi radio is the **onboard ESP32-C6** (ESP-Hosted SDIO, 2.4 GHz).
DIP must be **UART** so Crowtail MIDI owns GPIO 47/48. Do not also
init a XIAO C5 on UART1 — that path never synced and is abandoned.
Factory C6 1.x firmware needs a Hosted 2.12 slave image or SoftAP
stays down; MIDI and the local Link session still run.

This unit: ESP32-P4 **rev v1.3**, MAC `e8:f6:0a:e0:44:a3`, 32 MB HEX
PSRAM @ 200 MHz, IDF 5.5.5. Do not flash a `REV_MIN_301` image.

## Idle dimming

With `Idle dim after` set (web editor System page or SYSTEM > DIM on
the panel; default off), the backlight dims through the STC8 expander
PWM after that many seconds without a touch; a stopped transport
blanks entirely after three dim windows (a playing one only dims).
While blank, composition is skipped and the waking tap is swallowed so
it cannot press a control blind. `neon::ui::IdleDimmer`, host-tested.
