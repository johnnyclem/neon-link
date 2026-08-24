# CrowPanel 5.79" — MIDI IN / OUT (2× DIN + 6N138)

Perfboard dongle: two 5-pin 180° DIN jacks, one 6N138, five wires
back to the CrowPanel IDC. Firmware is already `TX=IO21`, `RX=IO38`.

OUT is a 3.3 V current loop (no opto). IN is a 6N138. The 6N138
Darlington is specified at **4.5–20 V**; the CrowPanel header has no
5 V. Run pin 8 from **USB 5 V** (common GND) and pull the open-collector
output up to **3V3** so the S3 never sees 5 V. A 3.3 V-only jumper is
documented as a fallback, not the happy path.

Do not feed 5 V into a CrowPanel 3V3 pin.

## Connector to the panel

Beep the IDC from outside the case — the two columns are mirrored.

| MIDI board | CrowPanel IDC | Notes |
|------------|---------------|-------|
| TX | **IO21** | UART1 MIDI OUT |
| RX | **IO38** | UART1 MIDI IN, after the opto |
| 3V3 | **3V3** | OUT loop + RX pull-up |
| GND | **GND** | common, including USB 5 V return |
| 5V | *not on the IDC* | 6N138 pin 8 only. USB-A 5 V, or VBUS pad **P2** if you open the case. |

![Schematic](schematic.png)

```
 CrowPanel                                          MIDI OUT J1
                                                    5-pin DIN 180°
 3V3 ─── R1 220 Ω ─────────────────────────────── pin 4
 IO21 ── R2 220 Ω ─────────────────────────────── pin 5
 GND ──────────────────────────────────────────── pin 2
                                                  pins 1, 3 NC


 CrowPanel              6N138 DIP-8                 MIDI IN J2
                        notch up                    5-pin DIN 180°
 5V ──┬── U1 pin 8 VCC
      │
     C1 100 nF
      │
 GND ─┴── U1 pin 5 GND ── U1 pin 7 ── R4 10 kΩ ── GND

 J2 pin 4 ── R3 220 Ω ── U1 pin 2 (anode)
 J2 pin 5 ────────────── U1 pin 3 (cathode)
 J2 pin 2 ── GND (shield only)
 D1 1N4148: cathode on pin 2, anode on pin 3

 U1 pin 6 (OC out) ──┬── R5 10 kΩ ── 3V3
                     └── IO38

 U1 pins 1 and 4 NC
 J2 pins 1 and 3 NC
```

```
 6N138 top view, notch up

   1 NC          8 VCC     ← 5 V (or 3V3 fallback)
   2 A           7 Base    ← R4 10 kΩ to GND  (do not leave open)
   3 K           6 OUT     ← IO38 + R5 to 3V3
   4 NC          5 GND
```

Idle MIDI = no LED current = transistor off = pin 6 pulled high =
UART idle. A start bit lights the LED, pin 6 falls. That is 31250 8N1.

## 5 V for the 6N138

Any of these, **GND common with the CrowPanel**:

1. Spare USB-A charger / power bank: +5 to pin 8, − to GND.
2. USB-C already in the panel: open the four screws, VBUS is test
   pad **P2** next to the USB jack. One wire. Do not also short it
   to 3V3.
3. 3.3 V fallback: jumper pin 8 to 3V3, change **R5 to 1 kΩ**, keep
   R4. Out of spec; many loops still clock. If IN is garbled, you
   need real 5 V.

## DIN numbering

Looking **into** the female jack, notch down:

```
    1     3
  4    2    5
     notch
```

Solder cups on the back are mirrored. Beep cup → hole. Pins 1 and 3
are no-connect on both jacks.

## BOM

| Ref | Value | Notes |
|-----|-------|-------|
| J1 | 5-pin DIN 180° female | MIDI **OUT** |
| J2 | 5-pin DIN 180° female | MIDI **IN** |
| U1 | 6N138 | DIP-8. Socket it. |
| R1, R2, R3 | 220 Ω ¼ W | 180–270 Ω is fine |
| R4 | 10 kΩ | 6N138 pin 7 → GND. Not optional. |
| R5 | 10 kΩ (5 V VCC) / **1 kΩ** (3.3 V VCC) | pin 6 pull-up to 3V3 |
| D1 | 1N4148 | reverse across the LED |
| C1 | 100 nF | pin 8 to GND, tight to the chip |
| C2 | 10 µF (optional) | 5 V bulk |
| | 8-pin DIP socket | |
| | 1×5 Dupont / screw terminal | 5V, 3V3, GND, TX, RX |

No 74HC14, no thru jack, no activity LED. Add those later if you want.

## Perfboard

Keep R1/R2 within a couple of centimetres of J1, R3/D1/U1 next to J2.
Socket the 6N138. Star-GND at U1 pin 5: shield, CrowPanel GND, USB 5 V
return all meet there. 5 V and 3V3 never meet except through R5.

Suggested Dupont order on the dongle: `5V  3V3  GND  TX  RX`.

## What it does on the glass

- **OUT:** Link-derived 24 PPQN + Start/Stop. Settings → MIDI CLK ON.
- **IN:** Start/Continue → PLAYING, Stop → STOPPED. Incoming clock is
  ignored (`clock_policy = kIgnore`). This box still *emits* Link clock.

If OUT is silent: wrong IDC column, or the synth wants Type B (swap
DIN 4/5 on **J1 only**). If IN is silent: 6N138 has no 5 V, pin 7 is
floating, or swap DIN 4/5 on **J2 only**.
