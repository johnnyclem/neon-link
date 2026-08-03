# NEON LINK — Software Architect Handoff

**Project**: Bidirectional Ableton Link + BLE MIDI Eurorack Module  
**Codename / Working Name**: NEON LINK (final name TBD)  
**Date**: 2026-07-28  
**Target Form Factor**: 8–10 HP Eurorack  
**Hardware Lead Context**: This document is the primary handoff for firmware / software architecture.

---

## 1. Project Goals & Competitive Position

We are building a superior alternative to the Circuit Happy ML:2m ($250, 2HP, WiFi-only, one-way, two outputs, no local display, no inputs).

### Primary Differentiators
| Capability                    | ML:2m              | NEON LINK (ours)                          |
|------------------------------|--------------------|-------------------------------------------|
| Directionality               | One-way            | True bidirectional (Clock/Reset In)       |
| Networking                   | WiFi only          | WiFi + RJ45 Ethernet                      |
| Outputs                      | 2 CV + optional MIDI | 4 independent clocks + Reset + Run + Tempo CV + TRS MIDI |
| Local UI                     | Buttons + LEDs     | OLED + encoder                            |
| BLE MIDI                     | None               | Full notes/CC/clock → CV/Gate + TRS       |
| Price target (components)    | N/A                | < $65–75                                  |

**Core promise**: A rock-solid Ableton Link peer that also functions as a wireless MIDI-to-CV bridge and multi-clock generator, with Ethernet reliability and a real local interface.

---

## 2. Hardware Platform

### MCU
- **ESP32-S3-WROOM-1** (N8 or N16R8 recommended)
- Dual-core Xtensa, WiFi + BLE onboard, sufficient GPIO and peripherals
- One core for networking / Link / BLE
- One core (or high-priority tasks) for real-time pulse generation

### Key Peripherals
- **Ethernet**: W5500 on SPI (prefer Ethernet when cable is present)
- **Display**: 0.96" or 1.3" SSD1306 / SH1106 OLED over I2C
- **User input**: Rotary encoder with push button
- **Status LEDs**: Network, Beat, Run (3×)
- **Outputs** (5 V logic level):
  - 4× independent Clock
  - 1× Reset / Start pulse
  - 1× Run / Stop gate
  - 1× Tempo CV (0–5 V, filtered PWM or MCP4725 DAC)
  - 1× TRS MIDI out (Type A)
- **Inputs**:
  - Clock In (protected, Schmitt)
  - Reset In
- **Power**: +12 V from Eurorack bus → efficient buck → 3.3 V. Target <150 mA.

### Real-time Requirements
- Clock/gate jitter must be low enough for musical use (sub-millisecond target, preferably much tighter).
- Link session state must be captured and acted upon with minimal latency.
- BLE MIDI and web UI must never starve or block the pulse engine.

---

## 3. Locked Feature Set (v1)

### Must-Have
- Ableton Link 3.x (tempo, phase, start/stop sync)
- WiFi (2.4 GHz) + Ethernet (W5500)
- Bidirectional operation:
  - Link → modular (classic behavior)
  - External clock → Link (measure period, set tempo, attempt phase alignment)
- 4 independent clock outputs with per-output:
  - PPQN / division / multiplication
  - Trigger length or square duty cycle
  - Shuffle
- Dedicated Reset pulse and Run gate
- Tempo CV output (0–5 V, software scalable)
- OLED local UI (BPM, phase bar, network status, current mappings, latency)
- Web-based editor (configuration, WiFi, firmware update style of ML:2m)
- Latency / delay compensation (adjustable from panel and web)
- TRS MIDI out
- **BLE MIDI** (notes, velocity, CCs, program change, clock, transport)
- Ability to fully disable BLE for maximum Link reliability

### Explicitly Out of Scope for v1
- Ableton Link Audio streaming
- Full polyphonic MIDI-to-CV voice allocation beyond simple note → pitch + gate
- Touch screen
- Battery / portable standalone mode

---

## 4. BLE MIDI Routing Matrix

| Incoming Message       | Default Behavior                                      | Configurable Options                          |
|------------------------|-------------------------------------------------------|-----------------------------------------------|
| Note On/Off            | Pitch → Tempo CV (1 V/oct scaled) + Gate on selected output | Route gate to any of the 4 clocks or Run     |
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
