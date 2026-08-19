# Stamp S3 USB MIDI

Turns an [M5Stamp S3](https://docs.m5stack.com/en/core/StampS3) into a
**class-compliant USB MIDI** interface. The S3 has USB OTG / TinyUSB;
the NanoC6 does not. Plug the Stamp into the Mac and Live sees
**link-sync MIDI** — no Hairless.

Bidirectional: CrowPanel clock in, and anything Live sends back out
G3 at 31250.

## Wire it (TTL)

CrowPanel MIDI out is 3.3 V UART on the IDC. No DIN / no opto into
the Stamp.

| CrowPanel IDC | Stamp S3 |
|---------------|----------|
| **IO21** (MIDI TX) | **G1** (RX) |
| **GND** | **GND** |

Optional: **G3** is UART TX if you want the Mac to drive a DIN later.

Do **not** join CrowPanel 3V3 to Stamp 5V. Power the CrowPanel from
its USB-C. The Stamp is bus-powered from the Mac.

```
CrowPanel IO21 ────── Stamp G1
CrowPanel GND  ────── Stamp GND
USB-C ── CrowPanel (power / console)
USB-C ── Stamp S3 ── Mac  (USB MIDI)
```

Onboard RGB: dim blue idle, flash green on traffic.

## Flash

Hold the **G0** button, plug USB-C in, then:

```bash
cd adapters/stamps3-usb-midi
pio run -t upload
```

After the first OTG image is on, the same USB-C is CDC + MIDI and
you should not need the button for later uploads.

If the port disappears after a bad flash: hold G0, tap reset / replug.

## Hear clock in Live

1. Plug the Stamp into the Mac. Audio MIDI Setup should list
   **link-sync MIDI**.
2. In Live, enable that input (Track / Sync).
3. Join the CrowPanel to the Link session (`LINK-EPD-C4A8` /
   password on the glass).
4. Press Play. 24 PPQN + Start/Stop on the Stamp. The e-paper
   will not tick.

## Pins

| Function | GPIO | Notes |
|----------|------|--------|
| MIDI RX | **1** | from CrowPanel IO21 |
| MIDI TX | **3** | optional out |
| RGB | 21 | WS2812, firmware-owned |
| Button | 0 | download; leave it |

The included HY2.0-4P pigtail can be soldered to G1 / G3 / GND / 5V
if you want a Grove cable. 5V is USB VBUS — still do not feed it
into the CrowPanel 3V3 rail.
