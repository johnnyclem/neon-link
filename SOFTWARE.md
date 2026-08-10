# NEON LINK — Software Architect Handoff

**Project**: Bidirectional Ableton Link + BLE MIDI Eurorack Module  
**Codename / Working Name**: NEON LINK / Formidable (final name TBD)  
**Date**: 2026-07-28 (updated 2026-08-09)  
**Target Form Factor**: 10 HP Eurorack (AMYboard base)  
**Hardware Lead Context**: This document is the primary handoff for firmware / software architecture.

**Related documents**:
- [`FEATURES.md`](FEATURES.md) — **Authoritative feature priority list** (Must / Should / Nice / Deferred)
- [`ADDENDUM_01-SOFTWARE.md`](ADDENDUM_01-SOFTWARE.md) — BLE MIDI standards, compatibility, latency, scope, and build guidance

---

## 1. Project Goals & Competitive Position

We are building a superior alternative to the Circuit Happy ML:2m ($250, 2HP, WiFi-only, one-way, two outputs, no local display, no inputs).

**Platform decision (2026-08-09)**: Primary hardware base is the **AMYboard** (ESP32-S3, 10HP, Eurorack power, 2× ±10 V CV I/O, TRS MIDI, I2C). We deliberately limit ourselves to two CV outputs. Additional clock divisions are left to mults and existing modules (Pam’s Workout, etc.).

### Primary Differentiators (v1)
| Capability                    | ML:2m              | NEON LINK / Formidable (v1)               |
|------------------------------|--------------------|-------------------------------------------|
| Directionality               | One-way            | **True bidirectional** (Clock In → Link)  |
| Networking                   | WiFi only          | WiFi (Ethernet later via add-on)          |
| Outputs                      | 2 CV               | Tempo CV + 1 flexible clock/gate + TRS MIDI |
| Local UI                     | Buttons + LEDs     | **OLED + encoder**                        |
| BLE MIDI                     | None               | **Full** notes/CC/transport → CV + MIDI   |
| Hardware cost                | ~$250 retail       | Extremely low (AMYboard ≈ $30)            |

**Core promise**: A rock-solid Ableton Link peer that also functions as a wireless MIDI-to-CV bridge, with Tempo CV and one flexible clock/gate output, in 10HP, at very low cost.

See [`FEATURES.md`](FEATURES.md) for the full prioritized list and success criteria.

---

## 2. Hardware Platform

### Primary Target: AMYboard
- **MCU**: ESP32-S3-WROOM-1 (dual-core, WiFi + BLE, PSRAM)
- **Form factor**: 10HP Eurorack with acrylic panel
- **Power**: Eurorack 10-pin (+12 V) + USB-C
- **Existing I/O**:
  - 2× CV out (±10 V, GP8413 DAC)
  - 2× CV in (±10 V, ADS1015 ADC)
  - TRS MIDI in + out
  - Stereo audio in/out (switchable line / modular levels)
  - S/PDIF in/out
  - Front-panel I2C (Grove) for OLED + encoder
  - MicroSD
- **Outputs for v1**: Tempo CV on one CV out; primary clock/gate on the second CV out (or buffered GPIO if needed)
- **Inputs for v1**: Clock In (and optional Reset In) on the two CV inputs

Ethernet (W5500) remains an optional later add-on. Custom multi-output PCBs are deferred until the software stack is proven.

### Real-time Requirements
- Clock/gate jitter must be low enough for musical use (sub-millisecond target, preferably much tighter).
- Link session state must be captured and acted upon with minimal latency.
- BLE MIDI and web UI must never starve or block the pulse engine.

---

## 3. Feature Set

**Authoritative prioritized list**: see [`FEATURES.md`](FEATURES.md).

### Must-Have (summary — see FEATURES.md for full detail)
- Ableton Link (WiFi) — tempo, phase, transport
- Bidirectional: external Clock In can drive Link tempo
- Tempo CV on one AMYboard CV out
- Primary configurable clock/gate on the second CV out
- BLE MIDI (notes / CCs / transport) → CV + TRS MIDI, fully disableable
- TRS MIDI I/O (already on board)
- OLED + encoder local UI via I2C
- Eurorack power + 10HP form factor (AMYboard)

### Explicitly Out of Scope / Deferred for First Hardware
- 4+ independent clock outputs
- Onboard Ethernet (external module later)
- Complex polyphonic MIDI-to-CV voice allocation
- Custom multi-output PCB before software is proven on AMYboard
- Ableton Link Audio streaming
- Touch screen
- Battery / portable mode as a primary goal

---

## 4. BLE MIDI Routing Matrix

| Incoming Message       | Default Behavior                                      | Configurable Options                          |
|------------------------|-------------------------------------------------------|-----------------------------------------------|
| Note On/Off            | Pitch → Tempo CV (1 V/oct scaled) + Gate on primary clock out | Route gate to available outs / Run behavior |
| Velocity               | Ignored or secondary filtered PWM (v1.1)              | Later expansion                               |
| CC                     | Software-mappable (latency, PPQN, shuffle, CV offset) | OLED + web menu                               |
| MIDI Clock             | Parallel or fallback to Link pulse engine             | Merge / replace / ignore                      |
| Start / Stop / Continue| Drive Run gate + influence Link transport             | Full transport control                        |
| Program Change         | Recall mapping / scene preset                         | Optional                                      |

**Design principle**: BLE MIDI is a high-value convenience and creative feature. The Link + high-priority timer path is sacred and must remain unaffected.

---

## 5. Recommended Software Architecture

### Dual-Core Strategy
- **Core 0 (Networking / Application)**:
  - WiFi + Ethernet (W5500 driver)
  - Ableton Link session (prefer `esp_abl_link` or official Ableton ESP32 path)
  - BLE stack + BLE MIDI parsing
  - Web server / configuration API
  - OLED rendering (lower priority)
  - Encoder / button handling
- **Core 1 (Real-time)**:
  - High-priority capture of Link session state
  - Hardware timer / RMT pulse generation for all clocks, Reset, and Run
  - External clock period measurement and tempo calculation
  - Minimal, deterministic code only

### Communication Between Cores
- Use lock-free or carefully synchronized queues / ring buffers for:
  - MIDI events → output engine
  - Configuration changes
  - Transport commands
- Avoid shared mutable state where possible. Prefer message passing.

### Timing Engine
- Maintain a high-resolution local timebase.
- On each relevant Link session state update, compute upcoming beat times.
- Schedule pulses with hardware timers or the RMT peripheral for clean edges.
- Support independent PPQN / mult / div per output.
- Implement latency compensation as a time offset applied to the schedule.

### Bidirectional Logic
1. **Link Master mode** (default): Follow Link tempo + phase. Generate outputs.
2. **External Clock Master mode**: Measure period on Clock In → call Link `setTempo`. Attempt phase alignment using reset or beat quantization heuristics.
3. Hybrid / quantize modes can be added later.

### Networking Preference
- Prefer Ethernet when a cable is detected (lower jitter, more reliable).
- Fall back to WiFi.
- Support Access Point mode for initial setup (same UX pattern as ML:2m).
- mDNS / `.local` hostname for the web editor.

### UI Requirements
- **OLED**:
  - BPM (large)
  - Phase / beat indicator
  - Network status (WiFi / Ethernet / BLE connected)
  - Current latency offset
  - Simple menu navigation via encoder
- **Web Editor**:
  - Output assignments and PPQN settings
  - Latency compensation
  - WiFi credentials and Ethernet preference
  - BLE enable / disable and pairing status
  - Firmware update path (if feasible)
  - Rhythm / creative clock modes (Euclidean, probability, jitter) — inherit and improve on ML:2m’s Rhythm Explorer

---

## 6. Suggested Starting Stack

- **Framework**: ESP-IDF (preferred for production control and real-time behavior). Arduino-ESP32 is acceptable for rapid prototyping but move critical paths to IDF-style tasks.
- **Ableton Link**: `esp_abl_link` component (tracks official Link) or the experimental official ESP32 example path. Validate current status of Ableton’s ESP32 support.
- **BLE MIDI**: Existing mature libraries (ESP32-BLE-MIDI / NimBLE-based solutions). Keep the stack on the networking core.
- **Display**: U8g2 or similar lightweight library for SSD1306.
- **Ethernet**: ESP-IDF W5500 / SPI Ethernet support.
- **Web UI**: Lightweight HTTP server (ESP-IDF httpd or equivalent) serving a simple SPA or server-rendered config pages.

---

## 7. Critical Risks & Mitigations

| Risk                              | Mitigation                                                                 |
|-----------------------------------|----------------------------------------------------------------------------|
| WiFi + BLE radio contention       | Core affinity, careful task priorities, ability to disable BLE             |
| Link timing jitter on WiFi        | Prefer Ethernet, implement solid latency compensation, HostTimeFilter-style filtering |
| BLE MIDI latency (10–30 ms typ.)  | Document clearly; treat as convenience, not primary clock source           |
| External clock → phase lock quality | Start with robust tempo following; iterate on phase heuristics             |
| Power / noise in Eurorack         | Hardware filtering is the first line; keep digital edges clean             |
| Feature creep                     | Strictly protect the real-time path; creative features are secondary       |

---

## 8. Development Milestones (Suggested)

1. **Skeleton**  
   ESP-IDF project, dual-core task structure, basic GPIO + timer pulse on one output.

2. **Link Peer**  
   Join a Link session over WiFi, print tempo/phase, generate a clean 4 PPQN clock from session state.

3. **Multi-output + Latency**  
   Four independent clocks + Reset + Run + adjustable latency compensation.

4. **Ethernet Path**  
   W5500 integration and preference logic.

5. **External Clock Input**  
   Period measurement → `setTempo` + basic phase handling.

6. **OLED + Encoder UI**  
   Local control of core parameters.

7. **BLE MIDI**  
   Receive notes/clock/transport, route to CV/Gate and TRS MIDI, with kill switch.

8. **Web Editor**  
   Full configuration surface + AP mode setup flow.

9. **Polish**  
   Rhythm Explorer modes, mapping presets, edge-case handling, power/thermal validation.

---

## 9. Open Questions for Software Architect

1. Preferred exact Link integration path (current best `esp_abl_link` vs any newer official support)?
2. RMT vs general-purpose timers for the multi-channel pulse engine — which gives cleaner independent PPQN control?
3. Minimum viable phase-alignment strategy when external clock becomes master?
4. How aggressive should we be with BLE connection interval tuning vs stability?
5. Web UI technology preference (vanilla JS, lightweight framework, or server-rendered)?
6. Do we need a simple MIDI merge engine on the TRS output when both Link-derived and BLE MIDI are active?

---

## 10. Reference Materials

- Circuit Happy ML:2m manual (full feature and UX reference for the web editor and output modes)
- Ableton Link GitHub + documentation
- `esp_abl_link` / existing ESP32 Link ports
- ESP32-S3 TRM (timers, RMT, dual-core guidelines)
- W5500 application notes
- Existing BLE MIDI libraries for ESP32

---

## 11. Success Criteria

A successful v1 firmware will:
- Maintain stable Link sync with low jitter over both WiFi and Ethernet
- Generate musically usable multi-channel clocks with per-output configuration
- Accept an external clock and drive the Link session tempo
- Receive BLE MIDI from Ableton (or other hosts) and produce useful CV/Gate + MIDI
- Be fully operable from the OLED + encoder without requiring a phone
- Stay within the power and real-time budgets of the chosen hardware

The real-time Link + clock path is the product. Everything else is leverage on top of that foundation.

---

**Handoff complete.**  
Please treat the dual-core separation and protection of the pulse engine as non-negotiable. Creative and convenience features (BLE MIDI, Rhythm modes, rich web UI) are highly valued but secondary to timing integrity.

Questions or architecture proposals welcome.
