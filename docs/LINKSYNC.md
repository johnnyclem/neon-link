# link-sync — Ableton Link MIDI-clock dongle

A thumb-sized dongle that joins an Ableton Link session over WiFi and
emits MIDI clock, start/stop/continue and song position out a single
TRS jack. Same repo, same GPLv2+ license, different build target.

**Reference board:** Seeed Studio **XIAO ESP32S3** (non-Sense).
A desk variant with a Waveshare 5.79" e-Paper is
[docs/LINKSYNC_EPD.md](LINKSYNC_EPD.md).
The zero-computer defaults and positioning proposal for this product
family is [docs/NEARBY.md](NEARBY.md).
A proposed spike to find the price floor — single-core ESP32-C3 with a
0.42" OLED — is [docs/SPIKE_ESP32C3_OLED.md](SPIKE_ESP32C3_OLED.md).
Reference board only. PRs welcome. **No support for arbitrary hardware.**

Shares with neon-link: Link integration, the timeline bus, the GPTimer
edge ring. Shares none of the audio engine.

## Why this module

The product's value is a stable wireless timing session. The XIAO ships
with a U.FL connector and a real antenna (rated 100 m+), 8 MB flash /
8 MB octal PSRAM, and the same ESP32-S3 dual-LX7 family as the AMYboard
— so the core-0 / core-1 split, the IRAM discipline, and the GPTimer
edge ring move across without a rewrite.

MIDI DIN cannot power this. Pin 4 is a current loop good for a few
milliamps; an S3 with the radio up draws ~100 mA. The dongle has its
own power: USB-C and the XIAO's battery charger.

```
   [USB-C]  ──  dongle  ──  [3.5 mm TRS]
   [LiPo]                        │
                            TRS-A cable
                            TRS-B cable     (passive adapters)
                            DIN-5 pigtail
```

One jack, three cables. TRS-A vs TRS-B is a tip/ring swap — a passive
adapter, not a firmware mode.

## Locked pin map

Do not move these without updating this file and the carrier.

| Function | XIAO pad | GPIO | Notes |
|----------|----------|------|-------|
| MIDI TX  | **D0**   | **1** | UART1 @ 31250. Not D2/GPIO3 (JTAG strap). |
| Jack sense / batt divider | D1 | 2 | Reserved, unpopulated in v1 |
| —        | D2       | 3 | **Do not use** — JTAG strap |
| User LED | onboard  | 21 | Inverted: LOW = on |
| —        | —        | 0, 45, 46 | Boot / VDD_SPI / ROM strap |

TRS Type A (MIDI Association default):

- D0 → 220 Ω → tip (DIN pin 5, data)
- 3V3 → 220 Ω → ring (DIN pin 4, source)
- GND → sleeve

TRS-B and DIN-5 are passive cables in the box.

**Battery polarity** on the XIAO pads: negative is the side *closest*
to the USB port. Getting this wrong destroys the board. Charge current
is fixed at 100 mA. Battery voltage is not readable without a carrier
divider (v1.1). On battery power there is no voltage on the 5V pin.

## What it does

Anyone can emit `0xF8` twenty-four times a beat. The hard half is
transport and phase, and that is what this target exists for:

- Start / Stop / Continue from Link's transport
- Song Position Pointer so joining a session at bar 17 lands the
  receiving device at bar 17
- Phase-correct start — Start only when SPP is 0; otherwise SPP +
  Continue, then clocks on the 24 PPQN grid

The portable scheduler is `neon::midi::ClockEngine`. Firmware submits
its events onto the same GPTimer ring the pulse engine uses; the ISR
writes the UART1 TX FIFO. A MIDI byte takes 320 µs; the edges are
placed to the microsecond.

`WIFI_PS_NONE` is mandatory. Modem sleep is what produced the 100 ms+
excursions measured on neon-link. A1 (octal-PSRAM flash stall,
`gap_max = 22.4 ms`) applies unchanged: deferred NVS commit and a 67 ms
refill horizon are already in the tree.

## Provisioning and status

No screen. First boot starts **BLE provisioning** (ESP-IDF manager,
security 1). Open the Espressif *ESP BLE Prov* app, pick `LSYNC-XXXX`,
enter the PoP printed on the USB console (`link-` plus 6 hex digits
from the MAC — the same secret as the SoftAP password).

Fallback after 90 s: SoftAP `LINK-SYNC-XXXX`. The SSID belongs on the
enclosure.

Single orange LED (LOW = on):

| State | Pattern |
|---|---|
| Unprovisioned | slow breathe, 2 s period |
| Connecting | fast blink, 5 Hz |
| Link, no peers | double-blink every 2 s |
| Link, peers, stopped | solid |
| **Playing** | **flash on the downbeat only** |

The downbeat flash is the diagnostic that matters. Charge state is the
separate red LED on the module.

## Scope

**In:** Link join, tempo follow, MIDI clock 24 PPQN, Start / Stop /
Continue + SPP, BLE provisioning, single-LED status, TRS-A default.

**Out (so they stop coming back):**

- USB MIDI — S3 has OTG, v1.1. USB-C is power and flashing for v1
- MIDI in / thru / merge
- RK-002-style scripting
- Battery telemetry — needs a carrier divider, v1.1
- Any second jack, any display, audio of any kind

## Definition of done

1. Cold boot to Link peer under 15 s, no user action after provisioning
2. Hardware synth stays in time over 30 min, no audible drift
3. Join a session already playing: downbeat lands on the correct beat
4. Tempo change in Live propagates within one beat
5. **Survives a router reboot** — reconnects without a power cycle
6. Runs ≥ 4 h on a 500 mAh cell (revise against S1)

Item 5 is the one people skip and the one that generates support email.
STA already retries forever with `WIFI_PS_NONE`; that path is shared
with neon-link.

## Spikes (hardware, not firmware)

- **S1.** Current draw with `WIFI_PS_NONE`. Seeed publish ~100 mA with
  default power save; measure it yourself. Gates battery and thermals.
- **S2.** RF sanity vs the AMYboard with the U.FL antenna installed.

## Build & flash

Requires ESP-IDF **v5.3.2** and the Ableton Link submodule.

```bash
cd neon-link
git submodule update --init --recursive
. ~/esp/esp-idf-v5.3.2/export.sh

idf.py set-target esp32s3
idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.linksync" build
./scripts/flash_linksync.sh
```

Install the U.FL antenna before any timing or RF test. A leftover
AMYboard `sdkconfig` (16 MB table) will be rejected by the flash
script — delete `sdkconfig` and re-run.

## Carrier notes

Leave unpopulated, cost nothing now:

- 6N138 / H11L1 footprint — MIDI in, if ever
- 2× resistor divider to an ADC pin — battery telemetry
- WS2812 — if single-LED status proves too cramped

Open questions: S1 current, S2 RF, antenna placement in the shell, and
whether the product wants a name other than the `link-sync` target.
