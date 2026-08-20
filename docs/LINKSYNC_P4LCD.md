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
| Panel | 800×480 RGB565, ~25 MHz PCLK |
| Console | CH343 on the UART USB-C (`/dev/cu.wchusbserial*`) |
| Touch | GT911 on I2C 45/46 (not used this pass) |
| Backlight | STC8H1K28 @ `0x2F` PWM |

C6 SDIO is **not** the Waveshare Function-EV map:

| Signal | GPIO |
|--------|------|
| CLK | 53 |
| CMD | 54 |
| D0..D3 | 52, 51, 50, 49 |
| RESET | 20 (active high) |

RGB data/sync eat GPIO 2–19, 40, 41. Do not enable the P4 EMAC — those
pins collide with SDIO.

MIDI TX is unset until the 7-pin / Crowtail UART is beeped. Clock still
runs internally.

## Build & flash

Device in download: hold **BOOT**, tap **RESET**, release BOOT.

```bash
. ~/esp/esp-idf-v5.5.5/export.sh
./scripts/flash_linksync-p4lcd.sh /dev/cu.wchusbserial210
```

Isolated tree: `build-linksync-p4lcd/` + `sdkconfig.linksync-p4lcd`.
Does not overwrite the S3 / e-paper `sdkconfig`.

First boot (this bring-up): the **800×480 panel is live** — BPM, STOPPED,
1–4 boxes, peers. Link runs a local session. MIDI UART is not pinned yet.

SoftAP is `LINK-LCD-XXXX` (password on the glass / `http://192.168.4.1`).
`esp_wifi_init` is what brings the C6 up over SDIO; do not pulse GPIO20
from the app. Create `WIFI_AP_DEF` *before* that init or lwIP panics
(`netif already added`) and the panel reboots every ~20 s.

The wireless-module DIP is only for the SX1262/nRF24 socket vs Crowtail
UART1. It does not affect the onboard C6.

This unit: ESP32-P4 **rev v1.3**, MAC `e8:f6:0a:e0:44:a3`, 32 MB HEX
PSRAM @ 200 MHz, IDF 5.5.5. Do not flash a `REV_MIN_301` image.
