# link-sync — Waveshare 5.79" e-Paper + ESP32-S3

A variant of the [link-sync dongle](LINKSYNC.md): same Ableton Link session,
same 24 PPQN MIDI clock + Start / Stop / Continue / SPP, same BLE
provisioning. The single LED is replaced by a **Waveshare 5.79" e-Paper
Module** (792×272, black/white, dual SSD1683).

**Reference hardware:** Waveshare 5.79" e-Paper Module wired to an
**ESP32-S3-DevKitC-1 N8R8** (8 MB octal PSRAM). Reference board only.
PRs welcome. **No support for arbitrary hardware.**

## Why this board

The XIAO dongle is pocketable and has no glass. This variant is the
studio / desk unit: a 139 × 48 mm panel you can read across a room —
tempo, transport, peers, WiFi — without opening a laptop. MIDI clock
still comes out the 2×10 IDC on the bottom of the case (GPIO21).

E-paper cannot flash the downbeat. That is a choice, not a bug. No
metronome, no big-beat animation, no partial chase. The panel
repaints only when the text changes (tempo, PLAYING/STOPPED, peers,
WiFi / setup AP) with a 5 s floor, then sleeps so the glass is not
held at high voltage. Clock lives on the GPTimer ring, same as the
XIAO.

While the setup AP is up (or the box is still unprovisioned) the
glass prints the SoftAP SSID and password. Physical access is the
credential.

## Locked pin map

Default is the **Elecrow CrowPanel 5.79"** all-in-one (S3 on the back).
The driver probes RST/BUSY at boot and will switch to the DevKit +
9-pin Waveshare module map if that is what is wired.

| Signal | CrowPanel (default) | DevKit + module |
|--------|---------------------|-----------------|
| DIN / MOSI | **11** | **11** |
| CLK / SCK | **12** | **12** |
| CS | **45** | **10** |
| DC | **46** | **9** |
| RST | **47** | **8** |
| BUSY | **48** | **18** |
| PWR | **7** (held HIGH) | **7** |
| MIDI TX | **21** (IDC header) | **4** |

GPIO 45/46 are strapping pins; they are fine after boot. CrowPanel
GPIO4 is the rotary NEXT switch — do not put UART there.

IDC → DIN / TRS / USB-MIDI adapters: [LINKSYNC_EPD_IDC.md](LINKSYNC_EPD_IDC.md).

## CrowPanel 2×10 IDC (only I/O without opening the case)

2.54 mm dual-row female, 20 pins. Silkscreen on the PCB (component
side) is:

```
IO8   IO3
IO14  IO9
IO16  IO15
IO18  IO17
IO20  IO19     ← S3 USB D+ / D− — leave free for a USB-MIDI pigtail
IO38  IO21     ← IO21 = MIDI TX
3V3   GND
3V3   GND
3V3   GND
```

From **outside** the case the two columns are mirrored. Beep IO21
to the silkscreen once; do not trust ribbon-cable pin 1 from a photo.

Do not put 5 V on any 3V3 pin.

## Bench adapters

One UART MIDI out (idle-high, 31250). Same two resistors for DIN and
TRS. 3.3 V current loop is enough for modern optos.

| Signal | CrowPanel | 5-pin DIN (OUT) | TRS-A (MMA) | TRS-B (old Korg) |
|--------|-----------|-----------------|-------------|------------------|
| TX | **IO21** → 220 Ω | pin 5 | **tip** | **ring** |
| 3V3 | header 3V3 → 220 Ω | pin 4 | **ring** | **tip** |
| GND | header GND | pin 2 | sleeve | sleeve |

### What to buy (no custom PCB required)

1. **2×10 2.54 mm male-to-Dupont** ribbon or a 20-pin GPIO breakout
   (search “2x10 IDC 2.54 to Dupont”). First test on a breadboard.
2. **DIN-5:** [Adafruit 1134](https://www.adafruit.com/product/1134)
   breadboard MIDI jack, or any panel DIN-5 female + two 220 Ω.
3. **TRS:** 3.5 mm stereo jack + the same two 220 Ω. A DPDT switch
   swaps tip/ring for A vs B. Off-the-shelf **Type-A MIDI DIN↔TRS**
   dongles (ExcelValley, etc.) only help *after* you have a DIN.
4. **USB MIDI is not a cable.** The USB-C on the CrowPanel is CH343
   UART0 (console), not native USB. To hear clock in a DAW today:
   DIN or TRS → any USB-MIDI interface you already own (UM-ONE,
   cheap “USB MIDI cable”, etc.).

Native USB-MIDI later is a firmware feature plus a second pigtail:
GPIO19 = D−, GPIO20 = D+, GND, board still powered from its USB-C.
Do not feed VBUS into 3V3.

### First live test

Play from Live → this box should emit 24 PPQN + Start/Stop on IO21.
A hardware synth on DIN/TRS, or a USB-MIDI interface into the Mac,
is the ground truth. The panel will not tick.

## Panel rules

Waveshare's own precautions apply:

- Full refresh only when the painted text changes, then sleep
- Do not treat the glass as a metronome

## Build & flash

```bash
idf.py set-target esp32s3
idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.linksync-epd" build
./scripts/flash_linksync-epd.sh
```

First boot: Espressif *ESP BLE Prov*, device `LSYNC-XXXX`, PoP on the
USB console. The panel shows the SoftAP SSID + password (and
`http://192.168.4.1`) until credentials land.

## Out of scope (same list as the XIAO)

Native USB MIDI (needs firmware + GPIO19/20 pigtail), MIDI in/thru,
battery telemetry, audio, a second jack. The e-paper is status only.
