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
| Extra outputs           | Optional MIDI clock       | Reset, Run gate, **Tempo CV**, TRS MIDI |
| Inputs                  | None                      | **Clock In + Reset In**                |
| Local UI                | Buttons + LEDs            | **OLED + encoder**                     |
| BLE MIDI (notes/CC)     | None                      | **Full support**                       |
| Aesthetic               | Minimal industrial        | **90s neon / Saved by the Bell**       |
| Price                   | $250                      | Components < $75                       |

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

## Documentation

| Document | Audience | Description |
|----------|----------|-------------|
| **[HARDWARE.md](HARDWARE.md)** | Hardware Engineer | Exhaustive hardware PRD — mechanical, electrical, power, I/O specs, BOM targets, prototype phases, acceptance criteria |
| **[SOFTWARE.md](SOFTWARE.md)** | Software / Firmware Architect | Full software architecture handoff — dual-core strategy, Link integration, BLE MIDI routing matrix, UI requirements, milestones, risks |
| **[EXECUTIVE_BRIEFING.md](EXECUTIVE_BRIEFING.md)** | Hardware Engineer (onboarding) | Concise strategic overview, competitive positioning, high-level targets, and what we need first |

Start with the Executive Briefing if you are joining hardware, then dive into `HARDWARE.md`.  
Software contributors should begin with `SOFTWARE.md`.

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

**Saved by the Bell / early-90s cool.**

- Neon pink/magenta, electric teal/cyan, hot yellow accents
- Black or deep purple panel base
- Bold geometric shapes, grids, abstract motifs
- Highly legible but playful typography
- The module should look like it belongs in a 1993 locker while remaining fully professional in use

---

## Firmware

Firmware development followed the nine milestones in [SOFTWARE.md](SOFTWARE.md) §8, one pull request per milestone — **all nine are implemented**: dual-core skeleton, Ableton Link peer, the six-output pulse engine with latency compensation, W5500 Ethernet with preference logic, external clock input driving the session, OLED + encoder UI, BLE MIDI with the §4 routing matrix, the web editor with AP setup, and the Rhythm Explorer / preset / instrumentation polish pass. Architecture decisions, the dual-core/pulse-engine design, and the proposed pinout live in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md). Bench validation on real hardware is the remaining step; each milestone PR carries its hardware-validation checklist, and the firmware self-reports output jitter on `/api/status`.

**Building** (ESP-IDF v5.3.x, target `esp32s3`):

```
idf.py set-target esp32s3
idf.py build flash monitor
```

**Host-native tests** (no ESP-IDF or hardware required):

```
cmake -S host -B build-host -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host -j
ctest --test-dir build-host --output-on-failure
```

CI builds the firmware and runs the host test suite on every pull request.

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

**Hardware Engineer**  
1. Read `EXECUTIVE_BRIEFING.md`  
2. Read `HARDWARE.md` in full  
3. Produce block diagram, power tree, proposed pinout, and first-pass BOM  

**Software / Firmware Architect**  
1. Read `SOFTWARE.md`  
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
