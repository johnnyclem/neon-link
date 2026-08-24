# link-sync — M5Stack Tab5 (ESP32-P4)

Ableton Link → MIDI-clock on the **M5Stack Tab5** (ESP32-P4NRW32 +
ESP32-C6, 720×1280 MIPI-DSI). Isolated from the CrowPanel RGB build.

## Hardware

| | |
|---|---|
| SoC | ESP32-P4NRW32, 16 MB flash, 32 MB HEX PSRAM |
| Radio | ESP32-C6 over SDIO |
| Panel | 720×1280 RGB565 MIPI-DSI (ILI9881C or ST7123/ST7121) |
| Console | native USB JTAG/serial (`/dev/cu.usbmodem*`) |
| I2C | SDA 31 / SCL 32 (PI4IOE 0x43/0x44, GT911/ST7123) |
| Backlight | LEDC PWM GPIO 22 |

C6 SDIO is **not** the CrowPanel map:

| Signal | GPIO |
|--------|------|
| D0..D3 | 11, 10, 9, 8 |
| CMD | 13 |
| CLK | 12 |
| RESET | 15 |
| WLAN_PWR_EN | PI4IOE 0x44 P0 |

Panel variant is probed at boot: ST7123 @ `0x55` vs GT911 @ `0x14`/`0x5D`
(ILI9881C). LCD_EN is PI4IOE 0x43 P4. Touch (ST7123, INT GPIO 23) is
enabled with LCD: tap **RUN/STOP**, **− / +** tempo, or the BPM digits
for tap-tempo. Hold ± to repeat. The gear opens the same settings
sections as the web editor (Outputs / Network / MIDI / Audio / System).

## Build & flash

Hold **RESET** ~2 s until the green LED flashes (download), or let
esptool auto-reset over USB JTAG.

```bash
. ~/esp/esp-idf-v5.5.5/export.sh
./scripts/flash_linksync-tab5.sh /dev/cu.usbmodem2101
```

Isolated tree: `build-linksync-tab5/`. Does not overwrite CrowPanel or
S3 sdkconfig.

SoftAP is `LINK-TAB-XXXX` (password on the glass). Grove HY2.0-4P
(GPIO 53/54) is reserved for MIDI later.
