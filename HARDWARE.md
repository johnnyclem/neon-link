# NEON LINK — Hardware Product Requirements Document (PRD)

**Version**: 1.0  
**Date**: 2026-08-03  
**Status**: Draft for Hardware Engineering onboarding  
**Related Documents**:  
- `EXECUTIVE_BRIEFING.md`  
- `HANDOFF.md` (Software Architect handoff)

This document is the authoritative hardware requirements source for designing and building the first several prototypes of NEON LINK.

---

## 1. Product Overview

### 1.1 Purpose
NEON LINK is an 8–10HP Eurorack module that provides bidirectional Ableton Link synchronization, multi-channel clock generation, Tempo CV, TRS MIDI, and BLE MIDI support, with both WiFi and Ethernet connectivity and a local OLED interface.

### 1.2 Primary Goals
- Deliver significantly more capability than the Circuit Happy ML:2m at substantially lower cost.
- Maintain excellent real-time timing performance (Link + clocks).
- Provide a usable local interface so a phone is not required for everyday operation.
- Achieve a distinctive 90s aesthetic while remaining fully functional and readable.
- Keep component cost under $75 for the complete bill of materials (qty 1 pricing).

### 1.3 Out of Scope (v1 Hardware)
- ~~Audio signal path / Link Audio streaming~~ — **in scope as of the
  AudioLink work** (`docs/AUDIOLINK.md`). Ableton shipped Link Audio in
  Link 4.0, and the AMYboard already carries the converters, so the
  deferral no longer bought anything. S/PDIF stays out.
- Battery operation
- USB host functionality beyond possible device-mode MIDI later
- Touch interfaces
- More than one Tempo CV channel

---

## 2. Mechanical Requirements

### 2.1 Form Factor
| Parameter              | Requirement                          | Notes |
|------------------------|--------------------------------------|-------|
| Height                 | 3U Eurorack (128.5 mm panel height) | Standard Doepfer-compatible |
| Width                  | **10HP preferred** (actual panel ≈ 50.5 mm) | 8HP acceptable if 10HP proves impossible |
| Depth                  | **≤ 40 mm** from rear face of panel to furthest component | Skiff-friendly target |
| Panel thickness        | 2.0–2.5 mm aluminum preferred       | PCB panel acceptable for early prototypes |
| Mounting               | Standard Eurorack M3 holes          | Left side required; right side recommended for 10HP |
| PCB max width          | (HP × 5.08 mm) – 2 mm               | Per EuroSynth / common practice |
| PCB max height         | ≤ 110 mm                             | To clear rails |

**10HP Actual Panel Width Reference** (common industry practice): ≈ 50.5 mm (slightly under 50.8 mm to allow tolerance).

### 2.2 Panel Layout (10HP Target)

**Recommended arrangement** (top to bottom):

**Top zone – UI**
- OLED display (0.96" or 1.3" SSD1306/SH1106 class)
- Rotary encoder with integrated push button (to the right of OLED)
- Three status LEDs: Network, Beat, Run (neon-colored preferred)

**Jack zone – two columns**

Left column (top → bottom):
1. CLK 1
2. CLK 2
3. CLK 3
4. CLK 4

Right column (top → bottom):
1. RESET
2. RUN
3. TEMPO CV
4. MIDI (TRS)
5. CLK IN
6. RST IN

Alternative tighter packing or 8HP variant may merge columns or reduce one clock if necessary. Final jack coordinates and clearance to be validated in CAD.

**Aesthetic**
- Base: black or deep purple / near-black
- Accents: neon pink/magenta, electric teal/cyan, hot yellow
- Style: bold geometric / Memphis-inspired 90s motifs (triangles, grids, abstract shapes)
- Typography: bold, highly legible, slightly playful 90s sans or geometric font
- Module name treatment should feel “Zack Attack” cool without sacrificing readability

### 2.3 Mechanical Constraints & Best Practices
- Provide adequate clearance behind jacks for cable strain relief.
- Avoid placing tall components directly behind the OLED or encoder.
- Ethernet MagJack may require careful placement (front edge or side) or a short internal cable if front-panel space is constrained.
- Design for standard Eurorack power cable clearance.

---

## 3. Electrical & Power Requirements

### 3.1 Power Input
- Standard Eurorack 10-pin or 16-pin power header.
- Red stripe = –12 V (Doepfer convention). Include clear silkscreen marking.
- Reverse-polarity protection required (series Schottky or ideal diode / MOSFET solution preferred).
- Primary rail used: **+12 V**.
- –12 V may be unused or lightly loaded; do not rely on it for core logic.
- +5 V rail on the bus is optional and should not be required.

### 3.2 Power Architecture
- +12 V → high-efficiency synchronous buck regulator → clean 3.3 V.
- Recommended class of parts: AP63205, TPS62130, or modern equivalents with good light-load efficiency and low noise.
- Heavy input and output filtering (LC + ferrite beads).
- Separate local decoupling for the ESP32-S3, W5500, and analog sections.
- Target total current draw from +12 V: **< 150 mA typical**, absolute maximum preferably < 200 mA under worst-case simultaneous WiFi + Ethernet + BLE + OLED activity.
- Provide a test point or easy measurement of 3.3 V rail current during bring-up.

### 3.3 Power Integrity & Noise
- Digital switching noise must be controlled; Eurorack environments are sensitive.
- Star grounding or careful split ground strategy between digital and any analog (Tempo CV) sections.
- Ferrite beads on power entry to the MCU and radio sections recommended.
- All switching regulators must be placed and filtered so they do not inject audible or measurable noise onto the Tempo CV output.

### 3.4 Protection
- Reverse polarity on power header.
- Input protection on Clock In and Reset In (series resistor + Schottky/TVS or equivalent to 3.3 V domain).
- ESD protection on all front-panel jacks (especially important for user-facing connectors).
- Output short-circuit tolerance desirable on digital outs.

---

## 4. MCU & Core Platform

### 4.1 Primary MCU
- **ESP32-S3-WROOM-1** (N8 or N16R8 preferred for PSRAM / flash headroom).
- Dual-core, WiFi 4, Bluetooth LE, sufficient GPIO, hardware timers, and RMT peripheral.

### 4.2 Critical Peripherals
| Function              | Interface / Notes                                      |
|-----------------------|--------------------------------------------------------|
| Ethernet              | W5500 on SPI + MagJack (RJ45)                          |
| Display               | SSD1306 / SH1106 OLED over I2C                         |
| User input            | Quadrature encoder + switch                            |
| Status LEDs           | 3× GPIO (or shift register if pin-constrained)         |
| Clock / Gate outputs  | Hardware timers or RMT for low jitter                  |
| Tempo CV              | Filtered PWM or low-cost DAC (e.g. MCP4725)            |
| MIDI TRS              | UART or bit-banged serial at 31.25 kbaud, Type A wiring|
| Clock In / Reset In   | GPIO with Schmitt / protection                         |

### 4.3 Real-Time Considerations (Hardware Implications)
- Clock and gate outputs must be driven from hardware timers or the RMT peripheral. Software bit-banging is not acceptable for the primary pulse path.
- GPIO chosen for clocks should have clean, low-capacitance routing and strong drive.
- SPI for W5500 should be on a high-speed capable bus with short traces.
- Crystal / clocking for the ESP32-S3 must be solid; avoid noisy power near the RF section.

### 4.4 Antenna & RF
- WiFi / BLE antenna: PCB trace antenna on the module or external U.FL + antenna. External antenna preferred if the module will live inside metal cases.
- Keep antenna clear of ground pours and noisy switching regulators.
- Simultaneous WiFi + BLE is required; validate coexistence and thermal behavior.

---

## 5. I/O Specifications

### 5.1 Digital Clock & Gate Outputs (CLK 1–4, RESET, RUN)
- Logic level: **5 V** preferred (level-shifted from 3.3 V if necessary).
- Drive strength: sufficient for typical Eurorack cable lengths and multiple inputs.
- Output impedance: low enough for clean edges; series damping resistor optional.
- Pulse characteristics (software controlled):
  - Trigger lengths typically 2 / 5 / 10 ms
  - Square modes with variable duty cycle
  - Independent PPQN / mult / div per output
- Rise/fall times should be fast and clean (no excessive ringing).

### 5.2 Tempo CV Output
- Range: **0–5 V** (software scalable; later 0–10 V option is desirable but not required for v1).
- Source: filtered PWM or dedicated DAC.
- Noise and ripple must be low enough for use as a pitch or modulation source.
- Output impedance suitable for Eurorack (1 kΩ or lower preferred).

### 5.3 MIDI TRS Output
- TRS Type A wiring (tip = current source / MIDI +, ring = MIDI –, sleeve = ground) or clearly documented alternative.
- 31.25 kbaud, standard MIDI current loop compatible via appropriate driver or open-drain + resistors.
- Can be driven from a UART.

### 5.4 Inputs (CLK IN, RST IN)
- Accept 0–5 V (or higher with protection) gate/trigger signals.
- Schmitt-trigger input characteristic preferred for clean edge detection.
- Series protection resistor + clamping to the 3.3 V domain.
- Software will measure period on CLK IN for external tempo following.

### 5.5 Connector Type
- 3.5 mm mono (or stereo where required) jacks, Thonkiconn / PJ301M / PJ398SM family or equivalent.
- Vertical PCB-mount preferred for modern Eurorack construction.
- Panel hole size typically 6.0–6.5 mm depending on bushing.

---

## 6. Connectivity Requirements

### 6.1 WiFi
- 2.4 GHz (ESP32-S3 native).
- Support for station mode + Access Point mode (for initial setup, same UX pattern as ML:2m).

### 6.2 Ethernet
- 10/100 via W5500.
- Standard RJ45 MagJack.
- Preference logic: when Ethernet link is detected, prefer it over WiFi for lower jitter and higher reliability.

### 6.3 Bluetooth Low Energy
- BLE MIDI support required.
- Must be able to run concurrently with WiFi (and Ethernet).
- Hardware must not preclude good RF performance; antenna design is critical.

### 6.4 Debugging / Programming
- Provide easy access to UART (TX/RX) and boot/strap pins for firmware development.
- USB-C for power + serial (and future class-compliant MIDI) is highly desirable on prototypes and final if space/cost allow.

---

## 7. User Interface Hardware

### 7.1 Display
- Monochrome OLED, 0.96" or 1.3", SSD1306 or SH1106 compatible, I2C.
- Must be readable in typical studio lighting.
- Mounted for easy viewing; avoid deep recess if possible.

### 7.2 Encoder
- Quality rotary encoder with integrated push switch.
- Detents preferred for menu navigation.
- Mechanical life and feel should be good (this is a primary user control).

### 7.3 Status LEDs
- Three LEDs with distinct colors or clear labeling:
  - Network (WiFi / Ethernet / BLE status)
  - Beat (pulses with tempo)
  - Run / Play
- Neon-colored LEDs preferred to match aesthetic.

---

## 8. Cost & BOM Targets

| Category                        | Target (qty 1)      |
|---------------------------------|---------------------|
| ESP32-S3 module                 | $5–7                |
| W5500 + MagJack                 | $6–12               |
| OLED                            | $2–4                |
| Jacks (8–9×)                    | $4–9                |
| Encoder + LEDs + passives       | $4–8                |
| Power regulation + filtering    | $2–4                |
| PCB + panel (prototype)         | $10–20              |
| **Total components**            | **< $75** (goal < $65) |

Early prototypes may exceed this slightly; production intent must hit the target.

---

## 9. Prototype Phases & Acceptance Criteria

### Phase 0 — Platform Bring-up
- ESP32-S3 DevKit or minimal breakout.
- Ableton Link session join over WiFi.
- Generate at least one clean clock output from Link session state.
- **Acceptance**: Stable Link peer, measurable low-jitter clock on a scope or logic analyzer.

### Phase 1 — Minimal I/O Board
- Custom or proto PCB with ESP32-S3, power section, 2–4 digital outputs, Clock In, basic status LED.
- **Acceptance**: Power rails clean, outputs switch cleanly at 5 V, external clock can be measured, no thermal issues.

### Phase 2 — Full Feature Prototype
- Complete 10HP (or 8HP) PCB + panel.
- All jacks, OLED, encoder, Ethernet, full power architecture.
- **Acceptance**:
  - Fits in a standard Eurorack case at target depth.
  - All outputs and inputs functional.
  - WiFi + Ethernet + BLE can operate (not necessarily fully featured firmware yet).
  - Tempo CV is usable and reasonably clean.
  - Current draw within budget.
  - Panel is mechanically solid and aesthetically aligned with the 90s direction.

### Phase 3 — Refined Prototype
- Layout and BOM optimization.
- DFM feedback incorporated.
- Multiple units built for software and field testing.
- **Acceptance**: Ready for limited external testing / early adopters.

---

## 10. Software–Hardware Interface Expectations

Hardware must expose:

- Clean, independently controllable GPIOs (or RMT channels) for the four clocks + Reset + Run.
- A stable timebase.
- Documented pinout and any required level translation.
- Reliable SPI for W5500 with chip-select and interrupt if available.
- I2C for OLED (and optional RTC).
- Protected digital inputs for Clock In and Reset In.
- A way to detect Ethernet link status if possible.

The software team will treat the real-time pulse generation path as highest priority. Any hardware decision that forces software bit-banging of clocks or introduces significant jitter will be considered a design defect.

See `HANDOFF.md` for full dual-core architecture, BLE MIDI routing matrix, and firmware milestones.

---

## 11. Testing & Validation Requirements

Hardware prototypes must support or enable:

- Current measurement on +12 V and 3.3 V rails.
- Scope probing of all clock/gate outputs and Tempo CV.
- Easy connection of logic analyzer to key GPIOs and SPI.
- RF performance checks (WiFi range, BLE pairing reliability, coexistence).
- Thermal imaging or temperature measurement under sustained load.
- Power-on and hot-plug behavior with reverse polarity testing (non-destructive).
- Mechanical fit in multiple Eurorack cases (including shallow skiffs).

---

## 12. Open Decisions & Risks for Hardware

1. **Exact width**: Confirm 10HP is achievable with the full jack complement + OLED + Ethernet MagJack.
2. **Ethernet physical placement**: Front-panel RJ45 vs internal MagJack + short cable vs side-facing.
3. **Antenna strategy**: PCB antenna vs U.FL + external.
4. **Tempo CV implementation**: Filtered PWM vs dedicated DAC (cost vs performance trade-off).
5. **5 V generation**: From 3.3 V boost, from +12 V LDO/buck, or direct if using a regulator that can supply both.
6. **USB-C**: Include on prototypes even if not on final panel?
7. **Thermal**: Validate simultaneous radio + Ethernet + OLED under continuous operation.
8. **Panel manufacturing**: Aluminum (preferred) vs PCB for early units; finishing and silkscreen method for neon aesthetic.

---

## 13. Deliverables Expected from Hardware Engineering

1. Block diagram and power tree.
2. Preliminary schematic (power + MCU + key I/O).
3. Proposed pinout table for ESP32-S3.
4. Mechanical drawing / panel layout with jack coordinates.
5. First-pass BOM with manufacturer part numbers and pricing.
6. Risk register and recommended prototype sequence.
7. Any deviations from this PRD with rationale.

---

## 14. Success Definition

Hardware is successful when the first full prototype:

- Physically fits and mounts correctly in a standard Eurorack system.
- Powers cleanly and stays within current budget.
- Provides low-jitter multi-channel clocks and a usable Tempo CV.
- Supports the full connectivity set (WiFi, Ethernet, BLE) without mutual destruction.
- Presents a panel that is both functional and on-brand with the 90s neon direction.
- Gives the software team a stable, well-documented platform on which to complete the firmware.

---

**End of Hardware PRD v1.0**

Questions, proposed deviations, or additional constraints should be raised early. This document will be updated as decisions are locked.
