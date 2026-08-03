# NEON LINK — Executive Briefing for Hardware Engineering

**Date**: 2026-08-03  
**Audience**: Hardware Engineer (onboarding)  
**Project**: Bidirectional Ableton Link + BLE MIDI Eurorack Module  
**Working Name**: NEON LINK (final name TBD)  
**Prepared by**: Project lead (Grok / team)

---

## 1. Why This Exists

Ableton Link is the modern standard for tempo and phase synchronization across devices. The only commercial Eurorack product that properly supports it is the Circuit Happy ML:2m — a $250, 2HP, WiFi-only, one-way clock generator with two outputs and no local display.

We are building a significantly more capable alternative at a fraction of the cost.

**Core promise**  
A rock-solid, bidirectional Ableton Link peer that also functions as a wireless MIDI-to-CV bridge and multi-channel clock generator, with Ethernet reliability, a real local UI, and a distinctive 90s aesthetic — all in 8–10HP and under $75 in component cost.

---

## 2. Competitive Snapshot (ML:2m)

| Feature                        | ML:2m                          | NEON LINK (target)                          |
|--------------------------------|--------------------------------|---------------------------------------------|
| Width                          | 2HP                            | 8–10HP                                      |
| Price                          | $250                           | Components < $75 → aggressive retail        |
| Directionality                 | One-way (Link → modular)       | True bidirectional                          |
| Networking                     | WiFi only                      | WiFi + RJ45 Ethernet                        |
| Clock outputs                  | 2                              | 4 independent                               |
| Additional outputs             | Optional MIDI clock            | Reset, Run gate, Tempo CV, TRS MIDI         |
| Inputs                         | None                           | Clock In + Reset In                         |
| Local UI                       | Buttons + LEDs                 | OLED + encoder + status LEDs                |
| BLE MIDI (notes/CC)            | None                           | Full support                                |
| Aesthetic                      | Minimal industrial             | Bold 90s “Saved by the Bell” neon geometric |

The ML:2m is elegant and compact. We win on capability, usability, reliability, and value.

---

## 3. High-Level Requirements

### Form Factor
- Preferred: **10HP** (actual panel width ≈ 50.5 mm)
- Acceptable fallback: 8HP
- Depth target: **≤ 40 mm** from rear of panel (skiff-friendly preferred)
- Standard Eurorack mounting (M3, Doepfer-compatible hole positions)
- Panel material: Aluminum preferred (or high-quality PCB panel for early prototypes)

### Electrical
- Powered from Eurorack bus (+12 V primary)
- Efficient regulation to 3.3 V
- Target current: **< 150 mA** typical
- Clean power (heavy filtering — digital modules can be noisy neighbors)
- Reverse polarity protection

### Core Platform
- **ESP32-S3-WROOM-1** (N8 or N16R8)
- Onboard WiFi + BLE
- SPI Ethernet via W5500 + MagJack
- Sufficient GPIO and hardware timers / RMT for low-jitter multi-channel pulse generation

### Must-Have I/O
- 4× independent Clock outputs (5 V logic)
- 1× Reset / Start pulse
- 1× Run / Stop gate
- 1× Tempo CV (0–5 V)
- 1× TRS MIDI out (Type A)
- 1× Clock In
- 1× Reset In
- OLED (0.96–1.3") + rotary encoder with push
- 3 status LEDs (Network / Beat / Run)

### Aesthetic Direction
90s cool / *Saved by the Bell* energy: neon pink/magenta, electric teal/cyan, hot yellow accents on black or deep purple base. Bold geometric / Memphis-inspired motifs. Playful but highly readable labeling.

---

## 4. Prototype Phasing (What We Need From Hardware)

| Phase | Goal                                      | Deliverable                                      | Priority |
|-------|-------------------------------------------|--------------------------------------------------|----------|
| 0     | Prove Link + basic clock on ESP32-S3     | Breadboard / DevKit bring-up                     | Immediate |
| 1     | Minimal viable I/O                        | Small PCB with ESP32-S3, power, 1–2 clocks, basic input | High |
| 2     | Full feature prototype                    | Complete 10HP PCB + panel, all I/O, Ethernet, OLED | Core |
| 3     | Refined / production-intent               | Layout clean-up, BOM optimization, DFM           | Follow-on |

Software will develop in parallel against the same milestones. Clean electrical interfaces and documented pinouts are critical so firmware can progress without waiting for final panels.

---

## 5. Success Criteria for Hardware

A successful first full prototype will:

1. Fit comfortably in a standard 10HP Eurorack space and ≤ 40 mm depth.
2. Draw well under 150 mA from +12 V with clean rails.
3. Provide low-jitter digital clock/gate outputs at 5 V.
4. Deliver a usable 0–5 V Tempo CV.
5. Survive normal Eurorack power connection abuse (reverse polarity, hot-plug).
6. Support simultaneous WiFi + Ethernet + BLE without excessive interference or thermal issues.
7. Present a panel that is both functional and unmistakably “90s cool.”
8. Stay inside the component cost target so the product can be priced aggressively against the ML:2m.

---

## 6. Key Documents Already Available

- `HANDOFF.md` — Software Architect handoff (architecture, dual-core strategy, real-time requirements, BLE MIDI routing, milestones). Hardware should treat the real-time pulse path as sacred.
- This Executive Briefing.
- Full Hardware PRD (separate document) — exhaustive requirements for design and build.

---

## 7. What We Need From You First

1. Confirmation of preferred width (10HP strongly preferred) and achievable depth.
2. Preliminary power architecture proposal (buck choice, filtering strategy).
3. High-level block diagram and proposed pinout for the ESP32-S3.
4. Any concerns about simultaneous WiFi + Ethernet + BLE + multi-channel RMT on the chosen module.
5. Rough first-pass BOM and risk list.

Welcome aboard. This is a high-leverage, clearly differentiated product with a realistic path to both a strong DIY/kit offering and a commercial module. Looking forward to collaborating.
