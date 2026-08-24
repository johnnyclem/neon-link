# CrowPanel IDC → MIDI adapters

Elecrow CrowPanel 5.79" (DIS08792E). The only I/O without opening the
case is the **2×10 2.54 mm IDC** on the bottom. Firmware UART1 MIDI is **GPIO21 TX** and **GPIO38 RX** on that
header (not GPIO4 — that is the rotary NEXT switch and never
reaches the connector). RX needs an optocoupler; TX does not.

Full shop notes (2× DIN + 6N138, BOM, battery, bring-up):
[elecrow_epaper_adapters.md](elecrow_epaper_adapters.md).


## What you need
- 5-pin DIN jacks, 2.54 mm pitch (breadboard / perfboard)
- 220 Ω resistors (two for OUT; one more for IN)
- **2×10 2.54 mm male-to-Dupont** ribbon, or a 20-pin GPIO breakout, or a 2×10 male pin header pressed into the socket with jumper wires.
- For TRS: 3.5 mm stereo jacks (optional DPDT to swap A/B).
- For IN/OUT on two DIN jacks: **6N138**, 1N4148, 220 Ω, 10 kΩ — see
  [adapters/crowpanel-midi-din](../adapters/crowpanel-midi-din/README.md).
- For USB-into-a-DAW this week: any USB-MIDI interface you already
  own (m5 nano?)

## Header pinout

2.54 mm dual-row female, 20 pins. Silkscreen on the **PCB component
side** (inside the case):

```
IO8   IO3
IO14  IO9
IO16  IO15
IO18  IO17
IO20  IO19     ← ESP32-S3 USB D+ / D−. Leave free.
IO38  IO21     ← MIDI RX (opto) / MIDI TX
3V3   GND
3V3   GND
3V3   GND
```

From **outside** the case the two columns are mirrored. Do not trust
ribbon-cable pin 1 from a photo. Beep **IO21** and **IO38** to the
silkscreen once (four corner screws). Any of the 3V3 / GND pairs is
fine. They sit on the same row — TX and RX as a pair.

Do not put 5 V on a 3V3 pin.

| Leave alone | Why |
|-------------|-----|
| IO3 | JTAG strap |
| IO4 | Rotary NEXT (not on this header) |
| IO19 / IO20 | Native USB D− / D+ |
| IO45–48, 11, 12, 7 | e-paper |

## The circuit (one current loop)

UART MIDI out, idle-high, 31250 baud. 3.3 V is enough for modern
optos.

```
CrowPanel 3V3 ──── 220 Ω ──── DIN pin 4   (TRS-A ring / TRS-B tip)
CrowPanel IO21 ─── 220 Ω ──── DIN pin 5   (TRS-A tip  / TRS-B ring)
CrowPanel GND  ────────────── DIN pin 2   (TRS sleeve)
```

Idle = TX high = no current. Start bit pulls TX low = current flows.
That is MIDI OUT. No opto on the transmit side.

DIN pins 1 and 3 are unused (no connect).

## MIDI IN / OUT on two DIN jacks

The circuit to build is
[adapters/crowpanel-midi-din](../adapters/crowpanel-midi-din/README.md):
J1 OUT (two 220 Ω, no opto), J2 IN (6N138). Pin 8 wants **5 V**; the
IDC has none — USB VBUS or pad P2, and pull pin 6 up to **3V3**. Do
not solder a DIN straight to IO38.

The ittybittymidi (active, battery + switch) is the same IN if you
would rather not solder the DIP: IBM tip → IO38, sleeve → GND. Passive
IBM is not a UART.

### 5-pin DIN (breadboard jack)

Looking into the female jack, notch down, pins numbered:

```
    1     3
  4    2    5
     notch
```

| DIN pin | Function | CrowPanel |
|---------|----------|-----------|
| 2 | GND (shield) | GND |
| 4 | +3.3 V via 220 Ω | 3V3 |
| 5 | TX via 220 Ω | **IO21** |

A 2.54 mm jack drops straight onto perfboard. Keep the two 220 Ω
within a few centimetres of the jack.

### TRS-A (MMA / most modern gear)

| TRS | DIN | CrowPanel |
|-----|-----|-----------|
| tip | 5 (TX) | IO21 → 220 Ω |
| ring | 4 (+V) | 3V3 → 220 Ω |
| sleeve | 2 (GND) | GND |

### TRS-B (older Korg / some Roland)

Tip and ring swapped vs A.

| TRS | DIN | CrowPanel |
|-----|-----|-----------|
| tip | 4 (+V) | 3V3 → 220 Ω |
| ring | 5 (TX) | IO21 → 220 Ω |
| sleeve | 2 (GND) | GND |

A DPDT switch on tip/ring makes one jack do both. Off-the-shelf
Type-A DIN↔TRS dongles only help *after* you have a DIN.

### USB MIDI

The USB-C on the CrowPanel is **CH343 UART0** (console / flash). It
is not native USB and will not enumerate as a MIDI device.

**Stamp S3 / RP2040:** real USB-OTG. Use
[adapters/stamps3-usb-midi](../adapters/stamps3-usb-midi/README.md)
(IO21 → G1) or
[adapters/rp2040-usb-midi](../adapters/rp2040-usb-midi/README.md)
(IO21 → Pico GP1). Both enumerate as **link-sync MIDI**.

**NanoC6:** Serial/JTAG only, cannot be USB MIDI. Use
[adapters/nanoc6-midi-bridge](../adapters/nanoc6-midi-bridge/README.md)
(BLE MIDI + Hairless) if that is what is in your hand.

**To hear clock in a DAW without BLE:** DIN or TRS → any USB-MIDI
interface (UM-ONE, a cheap “USB MIDI cable”, etc.).

**Native USB MIDI from this box** is a later firmware job (TinyUSB)
plus a second pigtail on the same IDC:

| USB | CrowPanel |
|-----|-----------|
| D− | IO19 |
| D+ | IO20 |
| GND | GND |
| VBUS | do **not** feed into 3V3 |

Keep the board powered from its own USB-C. The pigtail is a second
connector the computer sees as a MIDI device once the firmware exists.

## First live test

1. Plug the Dupont harness into the bottom IDC. Confirm 3V3 and GND
   with a meter. Beep IO21 (TX) and IO38 (RX).
2. Wire the two 220 Ω and the DIN **OUT** jack as above.
3. DIN cable into a hardware synth, or DIN → USB-MIDI interface → Mac.
4. Join `LINK-EPD-C4A8` / `link-55C4A8` (printed on the glass) so the
   box is on the same Link session as Live.
5. Press Play. You should see 24 PPQN + Start/Stop on the receiving
   end. The e-paper will **not** tick — status only, refresh on text
   change.
6. IN: IBM (or H11L1) into IO38/GND. A controller Start should flip
   the glass to PLAYING; Stop to STOPPED.

If the synth is silent, swap TRS tip/ring (you are on a Type-B input)
or check that IO21 is actually the pin you think it is.

## Firmware pin

`CONFIG_NEON_BOARD_LINKSYNC_EPD` sets `kPinMidiTx = 21` and
`kPinMidiRx = 38` in `components/neon_board/include/board_pins.h`.
Rebuild the isolated e-paper image if you change it:

```bash
idf.py -B build-linksync-epd \
  -DSDKCONFIG=build-linksync-epd/sdkconfig \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.linksync-epd" \
  build
```
