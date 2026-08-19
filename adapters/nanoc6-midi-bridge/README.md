# NanoC6 MIDI bridge

Turns an [M5Stack NanoC6](https://docs.m5stack.com/en/core/NanoC6) into a
MIDI dongle for the CrowPanel link-sync bench.

**The C6 cannot be a USB-MIDI class device.** Its USB-C is the
Serial/JTAG CDC block, not USB OTG / TinyUSB. macOS will not list it
under Audio MIDI Setup as “USB MIDI”. What this firmware actually is:

| Path | How it shows up | Use |
|------|-----------------|-----|
| **BLE MIDI** | `link-sync MIDI` in Audio MIDI Setup → Bluetooth | Native in Live. Prefer this. |
| **USB CDC** | `/dev/cu.usbmodem*` at 115200 | [Hairless MIDI](https://projectgus.github.io/hairless-midiserial/) if you want a cable |

Blue LED blinks on traffic. Three blinks at boot = we are up.

## Wire it (TTL, no opto)

CrowPanel MIDI out is 3.3 V UART on the IDC, not a DIN current loop.
Hook it straight to the Grove port:

| CrowPanel IDC | NanoC6 Grove HY2.0-4P |
|---------------|------------------------|
| **IO21** (MIDI TX) | **G1** yellow (RX) |
| **GND** | **GND** black |

Do **not** connect CrowPanel 3V3 to Grove 5V. Power each board from
its own USB-C.

```
CrowPanel IO21 ────── NanoC6 G1 (RX)
CrowPanel GND  ────── NanoC6 GND
USB-C ── CrowPanel     USB-C ── NanoC6 ── Mac
```

Grove white (G2) is UART TX at 31250 if you later want the Mac to talk
*to* a DIN. Unused for CrowPanel clock-out tests.

A DIN jack + two 220 Ω is the wrong interface **into** this chip.
Use that jack on the CrowPanel side for a hardware synth; use these
two wires for the NanoC6.

## Flash

```bash
cd adapters/nanoc6-midi-bridge
pio run -t upload
```

Hold the NanoC6 button if the port is stuck in the previous app.
`pio device list` should show a `usbmodem` after a successful boot.

## Hear clock in Live

1. Power both boards. NanoC6 blinks three times.
2. macOS **Audio MIDI Setup → Window → Show MIDI Studio → Bluetooth**.
3. Connect **link-sync MIDI**.
4. In Live, the same name is a MIDI input. Enable Track/Sync as you
   would any interface.
5. Join the CrowPanel to the Link session, press Play. 24 PPQN +
   Start/Stop should arrive. The e-paper will not tick.

### Hairless fallback (wired)

1. Open Hairless. Serial port = the NanoC6 `cu.usbmodem*`, baud
   **115200**. MIDI out = IAC or Live’s input.
2. Same Play test.

## Why not “real” USB MIDI

| Chip | USB | This job |
|------|-----|----------|
| ESP32-C6 (NanoC6) | Serial/JTAG CDC only | BLE MIDI + Hairless |
| ESP32-S3 / S2 / P4 | USB OTG + TinyUSB | Class-compliant USB MIDI |

You have a Stamp S3 — use
[../stamps3-usb-midi](../stamps3-usb-midi/README.md) instead. That one
is a real USB-MIDI gadget. The CrowPanel can also grow a USB-MIDI
pigtail on GPIO19/20 later — see [LINKSYNC_EPD_IDC.md](../../docs/LINKSYNC_EPD_IDC.md).
