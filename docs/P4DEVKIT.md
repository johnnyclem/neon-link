# NEON LINK on Waveshare ESP32-P4-Module-DEV-KIT

v2 R&D target. Not the AMYboard friends-and-family batch.

## A1 closed — flash stall is a product constraint

A1 rev2 ran on this kit's sibling (P4 rev v3.1, HEX @ 200 MHz, IDF 5.5.5)
and on AMYboard S3 (octal 80 MHz, IDF 5.3.2), same observer, same 4 MB
buffer. CSVs: `tools/a1_psram_stall/results_esp32s3_rev2.txt` and
`results_esp32p4_v31_rev2.txt`.

| | S3 | P4 @ 200 MHz |
|---|---|---|
| IDLE p50 | 519.5 ns | **449.3 ns** |
| PSRAM contention | 2.46× | **1.80×** |
| Flash stall (`gap_max`) | 22.4 ms | **51.0 ms** |
| Observer scheduled during FLASH | 11% | 4% |

The P4 wins the memory bus. It does not escape the flash stall, and
51 ms is longer than the audio DMA ring (`8 × 256` frames = 42.6 ms).
Both `gap_max` figures are one erase-write (the stressor yields between
ops). A real NVS commit is 2–3× that. A1's output is not "which chip"
— it is a constraint on any hardware this firmware ships on.

**v2 board rule:** config persistence must not sit on the same SPI
controller as the audio path. Rebuild this problem on P4 and you get
51 ms instead of 22. Next unknown that can still disqualify the chip
is A2 (ESP-Hosted SDIO jitter).

Ship-gate fallout for the AMYboard batch (G6 deferred NVS, G7 IRAM
pulse path) lives in `docs/FRIENDS_FAMILY_HANDOFF.md`. Those apply
here too if this kit ever plays audio.

**Board:** Waveshare ESP32-P4-Module-DEV-KIT (ESP32-P4NRW32 + ESP32-C6 over
SDIO, 16 MB flash, 32 MB HEX PSRAM).

A second P4 (different vendor, **rev v3.1 / eco6**) uses the same pin map
and OLED/C6/encoder bring-up, but **IDF 5.3.2 cannot boot it**. Build that
unit with IDF 5.5.5 and `sdkconfig.defaults.p4v31`:

```bash
. ~/esp/esp-idf-v5.5.5/export.sh
./scripts/flash_p4v31.sh          # or pass /dev/cu.usbmodemXXXX
```  
**Display:** 1.5" I2C OLED on the kit's I2C header — SSD1327 @ `0x3d`
(Adafruit 128×128 STEMMA) or SH1107 / SSD1306 @ `0x3c`.

## What works on this bring-up

| Piece | Status |
|---|---|
| OLED UI (128×128) | Yes — same `panel128` probe as AMYboard |
| Pulse engine / local timeline | Yes — virtual channels, no jacks |
| Ableton Link session object | Yes — local session; peers once C6 has a netif |
| WiFi / SoftAP | **Yes** — C6 via ESP-Hosted. Setup AP `NEON-LINK-XXXX` |
| BLE | Not this pass (hosted HCI is a separate path) |
| Ethernet (RJ45) | **Yes** — on-chip EMAC + IP101 RMII (IDF P4 defaults) |
| Link Audio | **Yes** — I2S via the onboard ES8311 3.5 mm jack |
| Audio (ES8311) | **Yes** — MCLK13 SCLK12 LRCK10 DOUT9 DIN11 PA53 |
| Encoder | KY-040 on GPIO 2/3/4 (40-pin header) |

## I2C header

Schematic net `ESP_I2C_*` (4-pin SH1.0 next to the I3C port, also
40-pin header pins 3 / 5, silkscreen SDA / SCL):

| Signal | GPIO | Also on this bus |
|---|---|---|
| SDA | 7 | ES8311 codec @ `0x18` |
| SCL | 8 | |

3.3 V, 2.2 kΩ board pull-ups plus internal pull-ups in `i2c_bus_init`.

Confirmed on the bench: **SH1107 @ `0x3c`** (128×128 mono, page mode).
`oled_ui` reports `panel kind=2`. The codec stays on the same bus at
`0x18`. If a rescan shows only `0x18`, the OLED is unplugged or on the
wrong header — check:

1. The 4-pin I2C port, not the I3C port and not a random 40-pin pair.
2. Pin order. Waveshare SH1.0 is not Grove (GND/VCC/SDA/SCL). A Grove
   or STEMMA cable on that header will power the panel wrong or swap
   the data lines.
3. 3.3 V only. 5 V OLEDs do not belong here.

## 3.5 mm jack (ES8311 + NS4150B)

Onboard headphone / speaker jack. This is the listen path for Link Audio
on this kit (there is no PCM3060 LINE OUT).

| Function | GPIO |
|---|---|
| I2S MCLK | 13 |
| I2S SCLK / BCLK | 12 |
| I2S ASDOUT (ESP DIN) | 11 |
| I2S LRCK | 10 |
| I2S DSDIN (ESP DOUT) | 9 |
| PA_Ctrl (NS4150B, active high) | 53 |
| Codec I2C | 7 / 8 @ `0x18` |

TRS stereo, not a modular line jack. Headphones or a powered speaker.
The amp is loud at full codec volume — firmware leaves DAC REG32 at ~75%.

## C6 radio (ESP-Hosted)

The module's ESP32-C6 is already the coprocessor. Host firmware pulls
`espressif/esp_hosted` 1.4 and `esp_wifi_remote` 0.14 (IDF 5.3), with
the Function-EV SDIO pin map that Waveshare's Brookesia image also uses
(CLK 18, CMD 19, D0–D3 14–17, reset GPIO 54).

C6 slave firmware ships on the module. Do not reflash the C6 unless
Hosted never prints `Received INIT event from ESP32 peripheral`.

`esp_wifi_init()` is what resets the C6 and waits for SDIO. Do not call
`esp_wifi_get_mode()` first — wifi_remote turns that into an RPC while
the transport is still down.

After boot with no stored STA credentials, SoftAP comes up as
`NEON-LINK-XXXX` (password on the OLED NETWORK screen, default
`link1234`). Join it and open `http://192.168.4.1/`.

Bench on this kit: `NEON-LINK-DD05` WPA2, 192.168.4.1, AP-only.

## Encoder

KY-040 (or any EC11 with a switch) on the 40-pin header:

| Encoder | P4 GPIO | Notes |
|---|---|---|
| CLK / A | 2 | Internal pull-up |
| DT / B | 3 | Internal pull-up |
| SW | 4 | Active-low, internal pull-up |
| + | 3V3 | |
| GND | GND | Commons for A/B/SW |

If rotation is backwards, swap A/B.

## TRS MIDI IN / OUT (40-pin header)

UART1 @ 31250, MIDI Association **Type A**. Not the Pimidi HAT — that
board is an I²C coprocessor; these pins are a plain UART current loop.

Do **not** use the header pads labelled TXD / RXD (physical 8 / 10).
Those are the USB-UART console (GPIO 37/38).

| Function | P4 GPIO | 40-pin (pin 1 = 3V3) |
|----------|---------|----------------------|
| MIDI OUT TX | **20** | **13** |
| MIDI IN  RX | **21** | **11** |
| 3V3 | — | 1 or 17 |
| 5V (6N138 VCC) | — | 2 or 4 |
| GND | — | 6, 9, 14, 20, 25, 30, 34, 39 |

### MIDI OUT (Type A)

```
3V3 ── 220 Ω ── ring
GPIO20 ── 220 Ω ── tip
GND ────────────── sleeve
```

Idle UART is high, so the loop is off until a start bit. Same circuit
as the XIAO link-sync dongle.

### MIDI IN (Type A) — 6N138

A MIDI current loop is not 3.3 V UART. Direct-wiring a TRS IN to GPIO21
will not speak MIDI and can stress the pin.

Feed the 6N138 from **5 V** (header pin 2/4). At 3.3 V VCC the 6N138 is
too slow for 31250. Pin 6 is open-collector: pull it up to **3V3** so
the P4 GPIO never sees 5 V.

DIP-8: 1 NC, 2 anode, 3 cathode, 4 NC, 5 GND, 6 out, 7 base, 8 VCC.

```
ring ── 220 Ω ── pin 2 (anode)     Type A = DIN 4, current source
tip  ────────── pin 3 (cathode)    Type A = DIN 5, current sink
sleeve ──────── GND (screen only)

pin 8 ── 5V
pin 5 ── GND
pin 7 ── 10 kΩ ── GND              speeds the falling edge
pin 6 ── GPIO21 and 10 kΩ to 3V3
```

If IN is dead-silent, swap tip and ring — that is Type B. Leave OUT as
drawn; a Type-B cable is a tip/ring swap, not a firmware mode.

### Two 5-pin DIN jacks (same UART, no firmware change)

DIN-5 is the same current loop. Type A TRS tip = DIN **5**, ring = DIN **4**.
Pin **2** is shield. Pins **1** and **3** stay unconnected.

Female chassis jack, **looking into the holes**, notch at the bottom:

```
      1           3      (NC)     (NC)
   4                 5   (+)      (−)
          2              shield
```

Solder cups on the back are mirrored. Pin 2 is always the centre pin.

**OUT** (no opto):

```
3V3    ── 220 Ω ── DIN pin 4
GPIO20 ── 220 Ω ── DIN pin 5
GND    ────────── DIN pin 2
```

**IN** (6N138, VCC from 5 V, GPIO pulled to 3V3):

```
DIN pin 4 ── 220 Ω ── 6N138 pin 2 (anode)
DIN pin 5 ────────── 6N138 pin 3 (cathode)
DIN pin 2 ────────── GND (screen only — not in the LED loop)

6N138 pin 8 ── 5V
6N138 pin 5 ── GND
6N138 pin 7 ── 10 kΩ ── GND
6N138 pin 6 ── GPIO21 and 10 kΩ to 3V3
```

Two separate jacks. Do not jumper OUT pin 4/5 onto IN — that is a
hardware thru box, not this circuit. If IN is mute, swap DIN 4 and 5
on the IN jack only.

Notes IN (and start/stop/clock) hit the same `MidiRouter` as BLE MIDI:
gates, transport, program change. Link-derived 24 PPQN clock still
leaves on OUT unless `clock_policy` is `replace`.

## Build & flash

```bash
git submodule update --init --recursive
. ~/esp/esp-idf-v5.3.2/export.sh

# First time, or after an S3/AMYboard sdkconfig:
rm -f sdkconfig
idf.py set-target esp32p4
idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.p4devkit" build

./scripts/flash_p4devkit.sh          # or pass /dev/cu.usbmodemXXXX
```

This kit's P4 is **rev v1.3**. The overlay pins `CONFIG_ESP32P4_REV_MIN_1`.
Do not force-flash a v3.x image.

USB Serial/JTAG on the Type-C port is the console. After flash, press
**RST only** if the app does not print `[neon] app_main enter`.
