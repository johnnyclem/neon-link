# link-sync — CrowPanel Advance 5.0" ESP32-P4

Ableton Link → MIDI-clock, same firmware path as
[link-epd](LINKSYNC_EPD.md), on the **Elecrow CrowPanel Advance 5.0"**
(ESP32-P4NRW32 + ESP32-C6, 800×480 IPS). The glass is RGB, so it can
tick. SoftAP is the provision path (no on-chip BLE).

## Hardware

| | |
|---|---|
| SoC | ESP32-P4NRW32 **rev v1.3**, 16 MB flash, 32 MB HEX PSRAM |
| Radio | ESP32-C6 over SDIO (ESP-Hosted 1.4) |
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

The **800×480 panel** is the Tab5 neon face in landscape: near-black
void, cyan tubes, hot-pink **STOP**. No encoders — drive it from the
glass. Controls sit in a left column (hero BPM, − / +, transport); the
right side is the **beat stage** — big, bright, and in motion while the
transport runs, dim and still while it is stopped, so run-state reads at
a glance without parsing the button text. **SYSTEM > BEAT DISP / BEAT
STYLE** pick the animation (giant number / pie / pendulum / pulse), same
as the OLED's full-screen beat page.

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
**AUDIO > AUDIO ON**, then **METRONOME ON**. CLICK sets the level, SOUND
picks the voice, and the click follows the transport — speakers tick only
while the panel shows a running clock. I2S stays down (G6) until the
engine is enabled, so a silent unit costs nothing.

Settings tabs match the web UI: **OUTPUTS**, **NETWORK**, **MIDI**,
**AUDIO**, **SYSTEM**. Tap a row to cycle/toggle; **−** / **+** on a
row nudge the value (hold to repeat). Flick the list to scroll.
**NETWORK** is a readout (Wi-Fi passwords still need a keyboard — use
the web editor). **SYSTEM > REBOOT** asks first. Changes apply live
and persist like the OLED menu.

GT911 is on I2C 45/46 (RST 36, INT 42). Address is `0x5D` or `0x14`
depending on INT during reset. First-boot log should show
`touch GT911 @0x.. ok` and `live panel 800x480 touch=1`.

Link runs a local session. MIDI is UART1 on GPIO 47/48 (Crowtail),
M5 Unit MIDI in Separate mode.

SoftAP is `LINK-LCD-XXXX` (password on the glass / `http://192.168.4.1`).
`esp_wifi_init` is what brings the C6 up over SDIO; do not pulse GPIO20
from the app. Create `WIFI_AP_DEF` *before* that init or lwIP panics
(`netif already added`) and the panel reboots every ~20 s.

The wireless-module DIP is only for the SX1262/nRF24 socket vs Crowtail
UART1. It does not affect the onboard C6.

This unit: ESP32-P4 **rev v1.3**, MAC `e8:f6:0a:e0:44:a3`, 32 MB HEX
PSRAM @ 200 MHz, IDF 5.5.5. Do not flash a `REV_MIN_301` image.
