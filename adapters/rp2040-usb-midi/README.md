# RP2040 USB MIDI

Turns a Raspberry Pi Pico / Pico W / XIAO RP2040 (any RP2040) into a
**class-compliant USB MIDI** interface. Same job as the Stamp S3
firmware; RP2040 TinyUSB is first-class for this.

CrowPanel clock in on UART @ 31250, USB-C enumerates as
**link-sync MIDI**. Bidirectional.

## Connectivity

The Pico’s USB-C is **only** the computer. MIDI from the CrowPanel is
two jumpers on the IDC, not USB.

```
  Mac
   │  USB-C  (class-compliant MIDI: "link-sync MIDI")
   │         also powers the Pico
   ▼
 RP2040 Pico
   GP1  RX  ←── wire ──  CrowPanel IO21  (UART MIDI TX @ 31250)
   GND      ←── wire ──  CrowPanel GND
                              │
                              │  USB-C  (power + console / CH343)
                              ▼
                         wall / Mac
```

Two USB cables. One MIDI pair. Nothing shares a USB-C.

| Cable | From | To | What it carries |
|-------|------|----|-----------------|
| USB-C | Pico | Mac | USB MIDI + Pico 5 V |
| USB-C | CrowPanel | Mac or a charger | CrowPanel power + UART0 console |
| Dupont | CrowPanel **IO21** | Pico **GP1** | MIDI clock (3.3 V UART) |
| Dupont | CrowPanel **GND** | Pico **GND** | common ground |

Do not plug the Pico into the CrowPanel USB-C. Do not join CrowPanel
3V3 to Pico 5 V / VSYS.

## Wire it (TTL)

CrowPanel MIDI out is 3.3 V UART. No DIN / no opto into the Pico.

| CrowPanel IDC | Pico / Pico W | XIAO RP2040 |
|---------------|---------------|-------------|
| **IO21** (MIDI TX) | **GP1** (UART0 RX) | **D7** (GP1) |
| **GND** | **GND** | **GND** |

Optional out: **GP0** (Pico) / **D6** (XIAO) is UART TX if you want
the Mac to drive a DIN later.

Do **not** join CrowPanel 3V3 to Pico 5V / VSYS. Power the CrowPanel
from its USB-C. The Pico is bus-powered from the Mac (USB).

```
CrowPanel IO21 ────── Pico GP1
CrowPanel GND  ────── Pico GND
USB-C ── CrowPanel (power / console)
USB    ── Pico     ── Mac  (USB MIDI)
```

On the **XIAO RP2040** the onboard NeoPixel is the status lamp
(GP12 data, GP11 power):

| Color | Meaning |
|-------|---------|
| dim red | USB not mounted |
| dim blue | USB up, stopped |
| dim green | playing |
| white flash | quarter-note (every 24 MIDI clocks) |

A generic Pico uses the onboard LED as a traffic blink only.

## Flash

Hold **BOOTSEL**, plug USB, then:

```bash
cd adapters/rp2040-usb-midi
pio run -e pico -t upload          # Pico
pio run -e pico_w -t upload        # Pico W
pio run -e xiao_rp2040 -t upload   # Seeed XIAO RP2040
```

Or drag ` .pio/build/pico/firmware.uf2 ` onto the RPI-RP2 volume.

## Hear clock in Live

1. Plug the Pico into the Mac. Audio MIDI Setup lists **link-sync MIDI**.
2. In Live, enable that input (Track / Sync).
3. Join the CrowPanel to the Link session (SSID / password on the glass).
4. Press Play. 24 PPQN + Start/Stop. The e-paper will not tick.

## Which adapter?

| Board | Firmware | Host sees |
|-------|----------|-----------|
| **RP2040 / Pico / XIAO RP2040** | this folder | USB MIDI |
| **Stamp S3** | [`../stamps3-usb-midi`](../stamps3-usb-midi/README.md) | USB MIDI |
| **NanoC6** | [`../nanoc6-midi-bridge`](../nanoc6-midi-bridge/README.md) | BLE MIDI + Hairless (no USB OTG) |
