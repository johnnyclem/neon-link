# NEON LINK

**Bidirectional Ableton Link + BLE MIDI Eurorack Module**

> A rock-solid Link peer, multi-channel clock generator, and wireless MIDI-to-CV bridge — with Ethernet, a real local UI, and unapologetic 90s neon energy. Built to be dramatically more capable than the $250 competition at a fraction of the cost.

**Status**: Hardware design & first prototype phase  
**Form Factor**: 8–10HP Eurorack (10HP preferred)  
**Component Cost Target**: < $75  
**Working Name**: NEON LINK

---

## Why This Exists

Ableton Link is the modern standard for tempo and phase sync across devices and apps. The only proper commercial Eurorack solution is the Circuit Happy **ML:2m** — a $250, 2HP, WiFi-only, one-way clock box with two outputs and no local display.

NEON LINK is designed to beat it on nearly every axis:

| Capability              | ML:2m                     | NEON LINK                              |
|-------------------------|---------------------------|----------------------------------------|
| Directionality          | One-way                   | **True bidirectional**                 |
| Networking              | WiFi only                 | **WiFi + RJ45 Ethernet**               |
| Clock outputs           | 2                         | **4 independent**                      |
| Output roles            | Clock / gate / reset      | Same list, **assignable per output**   |
| Extra outputs           | Optional MIDI clock       | Reset, Run gate, **Tempo CV**, TRS MIDI |
| Inputs                  | None                      | **Clock In + Reset In**                |
| Local UI                | Buttons + LEDs            | **OLED + encoder**                     |
| Rhythm Explorer         | Euclidean + free steps    | Same, **64 steps, 4 independent patterns** |
| Stored WiFi networks    | List with retry count     | Same, **plus wired Ethernet**          |
| Firmware update         | Vendor server             | **Upload from the editor, with rollback** |
| BLE MIDI (notes/CC)     | None                      | **Full support**                       |
| Aesthetic               | Minimal industrial        | **90s neon / Saved by the Bell**       |
| Price                   | $250                      | Components < $75                       |

Row-by-row against both competitor manuals:
**[docs/COMPETITIVE_PARITY.md](docs/COMPETITIVE_PARITY.md)**.

---

## Key Features (v1 Locked)

- Ableton Link 3.x (tempo, phase, start/stop)
- **Bidirectional**: external clock can drive the Link session
- WiFi 2.4 GHz + **Ethernet** (W5500)
- 4 independent clock outputs (per-output PPQN / mult / div, trigger length, square duty, shuffle)
- Dedicated Reset pulse + Run/Stop gate
- Tempo CV out (0–5 V)
- TRS MIDI out (Type A)
- **BLE MIDI** (notes, CCs, clock, transport) with routing to CV/Gate and TRS
- OLED local UI + rotary encoder
- Web-based configuration editor
- Latency compensation
- Distinctive 90s neon geometric panel design

---

## Architecture at a Glance

```
                    ┌─────────────────────────────────────┐
                    │           ESP32-S3                   │
                    │  ┌─────────────┐  ┌───────────────┐ │
   WiFi / BLE  <───►│  │ Core 0      │  │ Core 1        │ │
   Ethernet    <───►│  │ Networking  │  │ Real-time     │ │
   (W5500)          │  │ Link / BLE  │  │ Pulse Engine  │ │
                    │  │ Web / UI    │  │ (RMT/Timers)  │ │
                    │  └─────────────┘  └───────────────┘ │
                    └─────────────────────────────────────┘
                                      │
          ┌───────────┬───────────────┼───────────────┬──────────┐
          ▼           ▼               ▼               ▼          ▼
       CLK 1–4     RESET/RUN      TEMPO CV        MIDI TRS    CLK/RST IN
```

- **Core 0**: Networking, Ableton Link, BLE MIDI, web server, OLED
- **Core 1**: High-priority, low-jitter pulse generation from Link session state
- Real-time path is sacred — BLE MIDI and UI must never compromise clock integrity

---

## Visuals & Mockups

### 10HP Panel Concept (Photorealistic)
![NEON LINK 10HP panel mockup](docs/images/uxCeJ.jpg)

### 10HP Flat Panel Layout
![NEON LINK 10HP flat panel](docs/images/lwBHG.jpg)

### 8HP Variant (Photorealistic)
![NEON LINK 8HP panel mockup](docs/images/NO9KJ.jpg)

### 8HP Variant (Flat Layout)
![NEON LINK 8HP flat panel](docs/images/6ZyCS.jpg)

### System Block Diagram
![NEON LINK system architecture](docs/images/z6exi.jpg)

### Power & I/O Schematic Overview
![NEON LINK power and I/O schematic](docs/images/wRi6I.jpg)

> These are concept renderings for direction and discussion. Final mechanical drawings, exact jack coordinates, and production schematics will live in the hardware design files (KiCad, etc.).  
> **Note**: 10HP is preferred for comfort and full feature set. 8HP is a viable denser alternative if skiff space is critical.

### Pocket Operator Format Concept

Teenage Engineering Pocket Operator form factor — already supported in Eurorack via community adapters and mounts. This explores a portable / hybrid version of NEON LINK.

**3/4 View**  
![NEON LINK PO-style 3/4](docs/images/po/CoLkx.jpg)

**Front Product Shot**  
![NEON LINK PO-style front](docs/images/po/YVGb9.jpg)

**With Eurorack Adapter Concept**  
![NEON LINK PO in Eurorack mount](docs/images/po/RR3MI.jpg)

**Flat Technical Layout**  
![NEON LINK PO flat layout](docs/images/po/ayxA2.jpg)

> The PO format trades some I/O density for extreme portability and a different interaction model (button grid + small knobs). It can live standalone or drop into a Eurorack case via adapter. Feature prioritization for this form factor would likely focus on Link + 1–2 clocks + MIDI + BLE rather than the full 4-clock Eurorack complement.

---

## Documentation

| Document | Audience | Description |
|----------|----------|-------------|
| **[DESIGN_SYSTEM.md](DESIGN_SYSTEM.md)** | Design / Firmware / Web | **Authoritative design system** for both UI surfaces — tokens, status vocabulary, icons, the 128×128 layout, encoder interaction, and the `design/` → generator pipeline that keeps the two in step |
| **[HARDWARE.md](HARDWARE.md)** | Hardware Engineer | Exhaustive hardware PRD — mechanical, electrical, power, I/O specs, BOM targets, prototype phases, acceptance criteria |
| **[SOFTWARE.md](SOFTWARE.md)** | Software / Firmware Architect | Full software architecture handoff — dual-core strategy, Link integration, BLE MIDI routing matrix, UI requirements, milestones, risks |
| **[FEATURES.md](FEATURES.md)** | Everyone | **Authoritative feature priority list** (Must / Should / Nice / Deferred) after AMYboard pivot |
| **[EXECUTIVE_BRIEFING.md](EXECUTIVE_BRIEFING.md)** | Hardware Engineer (onboarding) | Concise strategic overview, competitive positioning, high-level targets, and what we need first |
| **[docs/AUDIOLINK.md](docs/AUDIOLINK.md)** | Software / Firmware | **AudioLink design spec** — Link 4.0 upgrade, sample-accurate audio engine on the AMYboard codec (metronome, pulses-as-audio, AMY synth), and Link Audio streaming to/from Live 12.4 |
| **[docs/COMPETITIVE_PARITY.md](docs/COMPETITIVE_PARITY.md)** | Everyone | Row-by-row audit against the Circuit Happy ML:2m and Missing Link Junior manuals |
| **[docs/TEENSY41.md](docs/TEENSY41.md)** | Hardware / Firmware | Teensy 4.1 build target — Ableton Link over native Ethernet, web editor, TRS MIDI, CLK/RST IN, audio engine, 2.8" SPI colour touchscreen (ILI9341 + XPT2046), two rotary encoders, 16 MB PSRAM; wiring and PlatformIO build |
| **[docs/DAISY.md](docs/DAISY.md)** | Hardware / Firmware | Daisy-family build targets — Seed with a 128×64 SSD1306/1309 OLED, plus headless Daisy Pod and Eurorack-native patch.init() configs; internal timeline (no network on this hardware), pulse engine, TRS MIDI, CLK/RST IN, audio engine on the built-in codec, Tempo CV on the true DAC, QSPI config store; wiring and Makefile/libDaisy build |
| **[site/index.html](site/index.html)** | Everyone | Marketing page + interactive manual for the device and the web editor — self-contained, renders the real firmware screens |
| **[docs/SCHEMATIC_OVERVIEW.md](docs/SCHEMATIC_OVERVIEW.md)** | Hardware / Firmware | High-level power, I/O, and core schematic description to accompany the diagrams |
| **[docs/BELA_GEM_SPEC.md](docs/BELA_GEM_SPEC.md)** | Hardware / Software | Design specification for implementing NEON LINK on the Bela Gem Multi platform |
| **[ADDENDUM_01-SOFTWARE.md](ADDENDUM_01-SOFTWARE.md)** | Software / Firmware | BLE MIDI standards, compatibility, scope, latency expectations, and build guidance |

Start with the Executive Briefing if you are joining hardware, then dive into `HARDWARE.md`.  
Software contributors should begin with `FEATURES.md` + `SOFTWARE.md` + `ADDENDUM_01-SOFTWARE.md`.  
For the Bela Gem platform variant, see `docs/BELA_GEM_SPEC.md`.

---

## Prototype Roadmap

| Phase | Goal | Key Deliverable |
|-------|------|-----------------|
| **0** | Platform bring-up | ESP32-S3 + Link session + one clean clock output |
| **1** | Minimal I/O | Power section + several clocks + Clock In on a small PCB |
| **2** | Full feature prototype | Complete 10HP PCB + panel, all I/O, Ethernet, OLED, encoder |
| **3** | Refined / production-intent | BOM optimization, DFM, multiple units for testing |

---

## Design Constraints (Non-Negotiable)

- **Width**: 10HP preferred (≈ 50.5 mm actual panel), 8HP acceptable
- **Depth**: ≤ 40 mm from rear of panel (skiff-friendly goal)
- **Power**: +12 V from Eurorack bus, < 150 mA typical, reverse-polarity protected
- **Cost**: Complete component BOM < $75 (stretch goal < $65)
- **Timing**: Hardware timers / RMT only for primary clocks — no software bit-banging
- **Aesthetic**: Bold 90s neon (pink / teal / yellow) geometric / Memphis-inspired panel on black or deep base

---

## Aesthetic Direction

**Saved by the Bell / early-90s cool — as attitude, not pastiche.**

- Neon pink/magenta, electric teal/cyan, hot yellow accents
- Black or deep purple panel base
- Bold geometric shapes, grids, abstract motifs
- Highly legible but playful typography
- The module should look like it belongs in a 1993 locker while remaining fully professional in use

This section describes the **physical panel**. For the two software surfaces —
the web configuration interface and the 128×128 display — the authority is
**[DESIGN_SYSTEM.md](DESIGN_SYSTEM.md)**, which narrows the same palette to a
dark-first, near-black ground with cyan as the "system is alive" accent and
magenta reserved for wireless. Both surfaces are generated from one set of
tokens in `design/`, so they cannot drift apart:

```bash
python3 scripts/gen_design.py          # tokens, strings, icons, numerals -> both surfaces
python3 scripts/check_contrast.py      # every text/background pair meets its target

cd web && npm ci && npm run build      # the page the module serves, gzipped into flash
npm run build:styleguide               # review both surfaces side by side, no hardware needed
```

---

## Licensing Notes

Ableton Link is dual-licensed (GPLv2+ and commercial).  
Open-source firmware using the Link library will fall under GPL obligations.  
A commercial license path from Ableton exists for closed-source / commercial products.  
This must be resolved before any public release or sale.

---

## Current Team Focus

- **Hardware**: Design power architecture, mechanical layout, first prototype PCBs and panel
- **Software**: Dual-core firmware skeleton, Link peer, multi-channel pulse engine, then BLE MIDI and UI

---

## Getting Started

### Run on AMYboard (primary v1 target)

The firmware builds for the shorepine **AMYboard** 10HP Eurorack module.
See **[docs/AMYBOARD.md](docs/AMYBOARD.md)** for the full I/O map, flash
instructions, and first-boot WiFi setup.

```bash
cd neon-link
git submodule update --init --recursive
. ~/esp/esp-idf-v5.3.2/export.sh          # ESP-IDF v5.3.2
idf.py set-target esp32s3
idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.amyboard" build
idf.py -p /dev/cu.usbmodem* erase-flash flash monitor
```

> The partition table now carries two OTA app slots so firmware can be
> updated from the web editor. The first flash after that change needs
> `erase-flash`; afterwards, `idf.py flash` — or the editor's **INSTALL
> UPDATE** button — is enough.

Host unit tests (no ESP-IDF):

```bash
cmake -S host -B build-host -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host -j && ctest --test-dir build-host --output-on-failure
```

### Run on Teensy 4.1 (touchscreen build)

The portable core also builds for a PJRC **Teensy 4.1** (16 MB PSRAM)
with a 2.8" SPI colour touchscreen and two rotary encoders — a full
peer of the ESP32 build: **Ableton Link over the Teensy's native
Ethernet** (custom asio-free platform layer), the same web editor at
`http://<device-name>.local/`, TRS MIDI clock, CLK/RST IN external
clock following, and the audio engine on an SGTL5000 shield. WiFi and
BLE stay ESP32-only (no radio on the Teensy). See
**[docs/TEENSY41.md](docs/TEENSY41.md)** for wiring and details.

```bash
pip install platformio
cd teensy41
pio run -t upload && pio device monitor
```

### Run on a Daisy Seed (SSD1306/1309 OLED build)

The portable core also builds for an Electrosmith **Daisy Seed**
(STM32H750, 64 MB SDRAM, built-in stereo codec) with a 128×64
SSD1306/SSD1309 OLED and two rotary encoders. No network interface on
this hardware, so no Link or web editor — the module runs its own
timeline (tap/nudge tempo, quantized transport) and follows CLK/RST IN;
TRS MIDI clock, the audio engine on the built-in codec, Tempo CV on the
true DAC, and config + presets in QSPI flash all work. See
**[docs/DAISY.md](docs/DAISY.md)** for wiring and details.

```bash
git submodule update --init --recursive
make -C third_party/libDaisy -j
make -C daisy -j              # Seed + OLED; or:
make -C daisy/pod -j          #   headless Daisy Pod (encoder/buttons/LEDs)
make -C daisy/patch_init -j   #   headless patch.init() (Eurorack-native jacks)
make -C daisy program-dfu     # hold BOOT, tap RESET first (any of the three)
```

**Hardware Engineer**  
1. Read `EXECUTIVE_BRIEFING.md`  
2. Read `HARDWARE.md` in full  
3. Produce block diagram, power tree, proposed pinout, and first-pass BOM  

**Software / Firmware Architect**  
1. Read `SOFTWARE.md` + `docs/FEATURES.md` + `docs/AMYBOARD.md`  
2. Validate current best Ableton Link integration path on ESP32-S3  
3. Stand up dual-core project skeleton and a single clean clock from Link session state  

---

## Open Questions

- Final product name confirmation (NEON LINK is the working title)
- Exact 10HP vs 8HP decision after mechanical packing study
- Ethernet MagJack placement strategy
- Tempo CV implementation (filtered PWM vs dedicated DAC)
- Antenna approach (PCB vs external)
- Ableton Link commercial licensing path if required

---

**NEON LINK** — more outputs, true bidirectional sync, Ethernet, BLE MIDI, and a local UI, without the $250 price tag.

Questions, proposed deviations, or prototype updates should be raised early so hardware and software stay aligned.
