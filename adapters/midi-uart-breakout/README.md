# UART MIDI breakout cable (RLCD and friends)

A lead that plugs into the unit’s UART header and becomes **MIDI IN +
MIDI OUT** at the far end. The tiny “MIDI chip” (opto + loop drivers,
3.3/5 V jumpers, DIN or TRS pads) lives in the unit-end lump so the
run is a real MIDI current loop, not 3.3 V UART over a metre of wire.

Two SKUs, same electronics:

| SKU | Far end |
|-----|---------|
| **TRS** | 2× 3.5 mm TRS plugs, MIDI Association **Type A** |
| **DIN** | 2× 5-pin 180° DIN plugs |

IN and OUT are separate plugs. MIDI is simplex; one jack is one
direction.

```
  RLCD 2×8 header
        │
        │  4 pins: VCC  GND  TX  RX
        ▼
  ┌─────────────────────┐
  │  ~20 × 8 × 16 mm    │  MIDI chip + 2.54 mm male
  │  jumpers = 3.3 V    │  (heat-shrink lump)
  └──────────┬──────────┘
             │  5-core + drain, 0.5–1.5 m
             │
        ┌────┴────┐
        ▼         ▼
     MIDI OUT   MIDI IN
     Type A     Type A
```

This is an accessory, not a neon-link target. No MCU in the cable.

## Unit end (the 20×8×16 mm lump)

The Waveshare RLCD’s expansion connector is a **2×8 2.54 mm female**.
The cable therefore uses **male** Dupont pins. A 2×8 shroud is ~20 × 9 ×
14 mm — that is the 20×8×16 mm envelope, with the MIDI chip stacked on
the back of the shroud and dual-wall heat-shrink over both.

Populate **only four pins**. Beep the silk; do not guess the 2×8 map
from memory (the two columns are easy to mirror).

| MIDI chip (right side) | RLCD net | GPIO |
|------------------------|----------|------|
| **VCC** | 3V3 | — |
| **GND** | GND | — |
| **TX** | U0TXD | **43** |
| **RX** | U0RXD | **44** |

Same four nets on the other UART boards (XIAO D6/D7, MaTouch 43/44,
C3 OLED 20/21). Swap the shroud pinout, not the chip.

**Jumpers on the chip: 3.3 V for both IN and OUT.** The S3 pins are
not 5 V tolerant. 5 V on VCC with the RX pull-up following it will
kill GPIO 44 the first time IN sees a start bit.

Key the shroud (blocked hole / missing pin on a NC position) so it
cannot mate flipped. Mark pin 1 on the shrink.

## Chip → jacks (left-side pads)

| MIDI chip | DIN | TRS Type A | Plug |
|-----------|-----|------------|------|
| **output_pin_4** | 4 (+) | **ring** | OUT |
| **output_pin_5** | 5 (−) | **tip** | OUT |
| **output_pin_2** | 2 (shield) | **sleeve** | OUT (and cable drain) |
| **input_pin_4** | 4 (+) | **ring** | IN |
| **input_pin_5** | 5 (−) | **tip** | IN |
| IN sleeve / DIN 2 | 2 | **sleeve** | IN — tie to **output_pin_2** / drain |

DIN pins 1 and 3 stay NC. Looking **into** a female jack, notch down:

```
    1     3          NC      NC
  4    2    5        +    shield    −
     notch
```

Male plugs are numbered the same way on the pins that go into that
jack. If OUT is silent on a Type-B synth, swap **tip/ring on the OUT
plug only**. Do not swap IN.

## Cable

Five conductors plus a drain, not a random rainbow Dupont lead.

| Conductor | Net |
|-----------|-----|
| 1 | OUT pin 5 / TRS tip |
| 2 | OUT pin 4 / TRS ring |
| 3 | IN pin 5 / TRS tip |
| 4 | IN pin 4 / TRS ring |
| 5 + foil drain | shield / pin 2 / both sleeves |

- **Wire:** 24 or 26 AWG, two twisted pairs (OUT 4/5, IN 4/5) plus
  drain. MIDI-grade 5-core with foil (the cheap “5-pin MIDI cable”
  guts) is ideal — cut the factory DIN off one end.
- **Length:** 0.5–1.5 m. Longer is fine; this is a current loop.
- **Y-split:** 8–12 cm from the plugs, so IN and OUT can reach two
  jacks on one box. Sleeve each leg separately and mark them.

Do **not** run 3.3 V UART down the long cable. The chip stays in the
header lump; only the loop rides the lead.

## Making it look like a cable

Best → good enough:

1. **Head lump (chip + 2×8 shroud)**
   - Hot-glue the chip to the back of the shroud so the Dupont pins
     take the strain, not the pad joints.
   - **Dual-wall adhesive-lined 3:1** (12 mm recovers to ~4 mm, but
     use 18–24 mm ID so it fits over the 20 mm shroud). One 40 mm
     length. Glue lining is the strain relief.
   - Optional: a dab of hot glue in the pin wells after the first
     test, before the shrink.

2. **Run**
   - **PET braid** (Techflex Flexo / generic 6 mm expandable) over
     the 5-core. Looks like an instrument cable, wears better than
     PVC.
   - Under the braid: the factory MIDI jacket if you harvested a
     cable, or 5 mm fabric/paracord sleeve.
   - Finish each end of the braid with a 10 mm band of 2:1
     glue-lined shrink so it cannot fray.

3. **Y-split**
   - 12 mm 3:1 glue-lined over the join. Glue lining fills the
     void where five cores become two legs.
   - Short legs in 4 mm PET or 6 mm 2:1 shrink.

4. **Plugs**
   - **TRS:** moulded 3.5 mm stereo (not mono TS). 8 mm glue-lined
     over the barrel + jacket. Heat-shrink colour rings:
     **black = OUT, red = IN** (or print IN/OUT on the shrink).
   - **DIN:** 5-pin 180° male with the usual moulded boot. Same
     colour language.
   - Do not use panel-mount jacks on a cable; those want a puck
     enclosure.

Skip: electrical tape as the outer jacket, liquid electrical tape
over the chip (rework nightmare), unlined 2:1 shrink on the head
(it slides off the shroud).

## BOM (one TRS cable)

| Qty | Item |
|-----|------|
| 1 | MIDI UART module, jumpers set **3.3 V IN and OUT** |
| 1 | 2×8 2.54 mm male Dupont shroud (or 1×4 if you only drop four pins) |
| 4 | Dupont male crimps, 2.54 mm |
| 0.5–1.5 m | 5-core + drain (or a sacrificed MIDI cable) |
| 1 | 6 mm PET braid, same length + 20 mm |
| 2 | 3.5 mm stereo TRS plugs, moulded or metal |
| — | Dual-wall 3:1: 24 mm (head), 12 mm (Y), 8 mm (plugs) |
| — | Hot glue |

DIN SKU: swap the two TRS plugs for two 5-pin 180° DIN males.

## First test

1. Jumpers 3.3 V. Continuity: no VCC-to-GND, no TX-to-GND.
2. Plug into the RLCD **before** applying shrink if this is the
   first one. Header silk TXD/RXD/3V3/GND.
3. OUT → synth MIDI IN (Type A). Play on the RLCD. 24 PPQN.
4. IN → a keyboard MIDI OUT. Notes should reach the unit (clock
   follow / notes per firmware). If IN is dead, the opto wants a
   real loop — confirm the keyboard is sourcing current, and that
   RX is 3.3 V not 5 V.
5. Then shrink.

## Not this cable

- USB MIDI to Live — that is still
  [`../rp2040-usb-midi`](../rp2040-usb-midi/README.md) (MCU in the
  USB plug, TX+GND only).
- A single TRS that is both IN and OUT — MIDI does not work that
  way. Two plugs.
