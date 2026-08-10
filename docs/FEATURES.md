# NEON LINK / Formidable — Feature Priority List

**Version**: 1.1 (AMYboard pivot)  
**Date**: 2026-08-09  
**Status**: Active  
**Related**: [`SOFTWARE.md`](SOFTWARE.md) · [`HARDWARE.md`](HARDWARE.md) · [`ADDENDUM_01-SOFTWARE.md`](ADDENDUM_01-SOFTWARE.md)

---

## Platform Decision (2026-08-09)

We are targeting the **AMYboard** (ESP32-S3, 10HP, Eurorack power, 2× ±10 V CV I/O, TRS MIDI, I2C expansion) as the primary hardware base for v1.

**Key simplification**: Only two analog CV outputs.  
Most users need at most two different clock values. Additional divisions/multiples are handled with mults + common modules (Pam’s Workout, etc.).

This keeps the product cheap, fast to prototype, and focused on the features that actually differentiate it from the ML:2m.

---

## Must-Have (v1 / MVP)

These define the product. Shipping without any of them means it is not yet the product.

| # | Feature | Implementation Notes |
|---|---------|----------------------|
| 1 | **Ableton Link** (WiFi) | Join/leave sessions, follow tempo + phase, stable long-term operation |
| 2 | **Bidirectional sync** | External Clock In (period measurement) can drive / set Link tempo |
| 3 | **Tempo CV Out** | One of the two ±10 V CV outs. Scalable (0–5 V, 1 V/oct, etc. later) |
| 4 | **Primary Clock / Gate Out** | Second CV out (or buffered GPIO if cleaner edges are needed). User-selectable PPQN and pulse width |
| 5 | **BLE MIDI** | Notes, CCs, transport → routing to CV outs + TRS MIDI. Fully disableable from UI |
| 6 | **TRS MIDI I/O** | Already present on AMYboard — treat as first-class |
| 7 | **Local UI** | OLED + encoder on the existing front-panel I2C port. Show Link state, tempo, BLE status, basic settings |
| 8 | **Eurorack power + 10HP** | Solved by the AMYboard itself |

### Core Product Promise

> A 10HP Eurorack module that is a solid Ableton Link peer **and** a BLE MIDI → CV/MIDI bridge, with Tempo CV and one flexible clock/gate output. Bidirectional. Extremely low hardware cost.

---

## Should-Have (strong v1 or very early v1.1)

| # | Feature | Notes |
|---|---------|-------|
| 9 | Reset / Run behavior | Map the second output (via menu) to Reset pulse or Run gate |
| 10 | Clock In + optional Reset In | Use the two CV inputs on the AMYboard |
| 11 | Web configuration page | Browser UI for PPQN, latency compensation, MIDI routing, advanced options |
| 12 | Latency / phase compensation | Manual or measured offset so clocks stay tight with Link |
| 13 | Multiple PPQN + shuffle options | On the primary clock output |
| 14 | Clear BLE enable/disable + status | Critical so users can maximize Link reliability when needed |

---

## Nice-to-Have / Later

- Second independent clock value on the second output (user chooses Clock 2 / Reset / Run / alternate CV)
- Ethernet via external W5500 module
- More sophisticated MIDI → CV voice allocation
- Preset system / SD card storage of settings
- Custom front panel with clearer labeling for the two outs
- Battery / portable experiments

---

## Explicitly Deferred / Out of Scope for First Hardware

- 4+ independent clock outputs
- Dedicated extra digital gate jacks beyond the two CV outs
- Onboard Ethernet
- High channel-count CV matrix
- Complex polyphonic MIDI-to-CV engine
- Any requirement for a custom PCB before the software stack is proven on the AMYboard

---

## Competitive Position (updated)

| Capability              | Circuit Happy ML:2m     | NEON LINK / Formidable (v1)              |
|-------------------------|-------------------------|------------------------------------------|
| Directionality          | One-way                 | **Bidirectional** (Clock In → Link)      |
| Networking              | WiFi only               | WiFi (Ethernet later)                    |
| Outputs                 | 2 CV                    | Tempo CV + 1 flexible clock/gate + MIDI  |
| BLE MIDI                | None                    | **Full** (notes/CC/transport → CV + MIDI)|
| Local UI                | Buttons + LEDs          | **OLED + encoder**                       |
| Form factor             | 2HP                     | 10HP                                     |
| Hardware cost target    | ~$250 retail            | **Very low** (AMYboard base ≈ $30)       |

The two outputs are enough. Mults and clock dividers already exist in every rack.

---

## Success Criteria for v1

A successful first firmware release on the AMYboard will:

- Join and stably follow an Ableton Link session
- Accept an external clock and drive Link tempo from it
- Output a useful Tempo CV
- Output at least one clean, configurable clock/gate
- Accept BLE MIDI notes/CCs and route them to CV and/or TRS MIDI
- Allow BLE to be completely disabled
- Provide a usable OLED + encoder interface
- Run reliably from Eurorack power

---

**End of FEATURES.md**

This document is the authoritative source for feature priority and scope. All software and hardware work should reference it.
