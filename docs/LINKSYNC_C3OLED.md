# link-sync — ESP32-C3 0.42" OLED stamp

A stamp-sized Ableton Link peer with an onboard 72×40 OLED. Same repo,
same GPLv2+ license, same MIDI-clock path as the XIAO dongle
([docs/LINKSYNC.md](LINKSYNC.md)). The glass is the status surface.

**Reference board:** ACEIRMC / generic **ESP32-C3 Super Mini with 0.42"
OLED** (Amazon ASIN B0H2GWGP9Q and clones). 20×25 mm, USB-C, 4 MB
embedded flash, no PSRAM. Listings sometimes call the panel 0.25".

```
   [USB-C]  ──  stamp  ──  OLED 72×40
                 │
              GPIO20 TX / GPIO21 RX  (DIN + 6N138)
```

## Why this board

The C3 is unicore RISC-V at 160 MHz with 384 KB SRAM. That is enough
for Link + WiFi + a 360-byte 1-bit framebuffer, and not enough for
NimBLE at the same time. BLE MIDI and BLE provisioning stay off; first
boot is SoftAP and the password is on the glass.

## Locked pin map

| Function | GPIO | Notes |
|----------|------|-------|
| OLED SDA | **5** | SSD1306-compatible @ 0x3C. Shared with Grove I2C units |
| OLED SCL | **6** | 400 kHz |
| Blue LED | **8** | Inverted: HIGH = off. Boot strap — firmware PWM is fine |
| BOOT     | **9** | Active-low. Short click = next page, long press = play/stop |
| MIDI TX  | **20** | UART1 @ 31250. Default; crossed vs silk TX. `--tx-pin` |
| MIDI RX  | **21** | UART1 @ 31250. Default; crossed vs silk RX. `--rx-pin` |
| USB D−/D+ | 18/19 | Native Serial/JTAG. Do not reuse |
| 5V / 3V3 / GND | rails | Grove red prefers **5V** (ByteButton LDO). MIDI can run on 3V3 |

Pulse channels are virtual. There are no Eurorack jacks on the stamp.

Production DIN MIDI (2× jack + 6N138, BOM and schematic):
**[docs/LINKSYNC_C3OLED_MIDI.md](LINKSYNC_C3OLED_MIDI.md)**.

OLED init is the EastRising 0.42" SSD1306 sequence (mux 0x27, column
window 28..99, 5 pages). Visible area is 72×40.

## What it does

- Joins an Ableton Link session over WiFi
- MIDI clock, Start / Stop / Continue, song position out GPIO20; MIDI IN on GPIO21 (crossed vs the silk TX/RX labels on this stamp)
- Four OLED pages, cycled with BOOT or ByteButton B6:
  1. **LIVE** — tempo, beat dots, PLAY/STOP, peer count, net state
  2. **NET** — STA SSID, IP, RSSI
  3. **SETUP** — SoftAP SSID, password, `192.168.4.1`
  4. **CTRL** — MIDI CLK, clock source, quantum (caret = B4/B5, edit = B2/B3)
- Blue LED uses the same patterns as the XIAO dongle (breathe /
  blink / downbeat flash)

## Provisioning

No BLE. First boot raises SoftAP `<device>-XXXX` (default name
`link-c3`) and lands the OLED on SETUP. Join that network, open
`http://192.168.4.1`, store a studio WiFi. After that, LIVE is the
boot page.

## Radio (read this if the AP is invisible)

These stamps have **no U.FL**. The antenna is a 2–3 mm ceramic chip
next to the 40 MHz crystal and GPIO21. Stock TX power (~19 dBm)
reflects into the PA: the driver says the AP is up, phones see
nothing. That is a known Super Mini flaw, not a missing connector.

Firmware workaround (always on this target):

- GPIO20/21 are the MIDI UART (TX=20, RX=21 on this stamp). The Super
  Mini antenna workaround still caps TX at 8.5 dBm; those two pins are
  not pulled down because MIDI owns them.
- TX capped at **8.5 dBm**
- 802.11b/g/n, HT20
- Boot listen-probe: the SETUP page bottom line is `8.5dBm Nn`
  (`N` = nearby 2.4 GHz APs the radio can hear)

If `N` is 0, RX is dead too (USB cable sitting on the antenna, or a
board with no matching network). Unplug from the laptop, use a short
USB extension, keep metal and the computer a few inches away, retry.

A 31 mm wire soldered to the antenna pad (the “one-wire” mod) is the
actual hardware fix. Reducing TX is what we can do in software.

## Grove accessories (M5Stack)

The two jacks on Unit ByteButton (U192) are **both Port A I2C**
(GND / 5V / SDA / SCL). They pass the I2C bus through. They are **not**
UART. Daisy-chaining the SAM2695 / Unit MIDI off the second jack puts
MIDI TX/RX onto SDA/SCL and neither device works.

UART is point-to-point. I2C is a shared bus. Split them:

```
C3 GPIO20 TX / GPIO21 RX + 3V3/GND  ── UART 31250 ──  SAM2695 Unit MIDI
C3 GPIO5  SDA / GPIO6  SCL + 5V/GND ── I2C 400 kHz ──  ByteButton @ 0x47
                                                       (OLED is already @ 0x3C)
```

Wire two 4-pin leads off the stamp (it has no Grove jack). ByteButton
Grove red is 5V into an LDO — use the stamp **5V** pin when you can;
3V3 often browns out the STM32. The second ByteButton jack is for
another I2C unit (8Encoder @ 0x41, Unit Encoder @ 0x40, a second pad
with a readdressed 0x47), not for MIDI.

### ByteButton map (left → right, bit0 → bit7)

| Key | Short | Hold / repeat | Combo |
|-----|-------|---------------|-------|
| B0 | play / stop | stop now | B0+B1 stop now; B0+B6 play now |
| B1 | tap tempo | — | |
| B2 | −1 BPM (CTRL: edit −) | auto-repeat | |
| B3 | +1 BPM (CTRL: edit +) | auto-repeat | |
| B4 | −10 BPM (CTRL: row up) | auto-repeat | |
| B5 | +10 BPM (CTRL: row down) | auto-repeat | |
| B6 | next page | cycle quantum 1/2/4/8 | |
| B7 | MIDI CLK OUT on/off | cycle clock source AUTO/LINK/MIDI | |

LEDs: B0 play, B6 page colour, B7 MIDI CLK, centre LED8 follows the beat
(magenta on 1). BOOT still works if the pad is unplugged.

## Out of scope

- BLE MIDI / BLE provisioning (RAM)
- Audio / Ethernet
- Dual-core pulse isolation — everything shares the one RISC-V core.
  GPTimer still owns the MIDI edges; the UI task is priority 3. The
  ByteButton poll is a 5 ms task on the same core.

## Build & flash

Requires ESP-IDF **v5.3.2** and the Ableton Link submodule.

```bash
cd neon-link
git submodule update --init --recursive
. ~/esp/esp-idf-v5.3.2/export.sh

./scripts/flash_linksync-c3oled.sh
./scripts/flash_linksync-c3oled.sh --tx-pin 20 --rx-pin 21
```

The script uses `build-linksync-c3oled/` and `sdkconfig.linksync-c3oled`
so it does not clobber an S3 tree. USB Serial/JTAG can enter download
without the BOOT button. After a flash the script uses a watchdog
reset so the chip leaves download mode (`--after hard_reset` on this
port samples GPIO9 low and sits in `waiting for download`). Hold BOOT,
tap RESET, release BOOT only if auto-reset misses the ROM loader.

A leftover 8 MB `sdkconfig` is rejected by the flash script — delete
`sdkconfig.linksync-c3oled` and re-run.

## Nearby spike (Neon Sync, no Ableton Link)

The same stamp can run **Neon Sync** — our own leaderless session
protocol behind the same `ILinkSession` seam ([docs/NEARBY.md](NEARBY.md),
[docs/NEON_SYNC.md](NEON_SYNC.md)). No GPL Link, no asio. Peers discover
each other on `239.77.83.78:20809` and do **not** interoperate with
Ableton Link peers. MIDI clock in/out is unchanged.

Isolated build dir + sdkconfig, so this does not overwrite the working
Link tree:

```bash
./scripts/flash_linksync-c3oled-nsync.sh --build-only
./scripts/flash_linksync-c3oled-nsync.sh            # flashes; overwrites Link firmware
```

Splash reads `NEON` / `nearby`; SoftAP default name is `near-c3-XXXX`.
The post-build step fails if `ableton::` or `asio::` symbols landed in
the ELF.

## RAM budget

C3 has no PSRAM. This overlay turns Bluetooth off, shrinks the WiFi
RX rings, and keeps the OLED flush to 360 bytes. If `app_main` boots
and then the heap collapses on STA associate, the next knob is
`CONFIG_LWIP_MAX_SOCKETS` (already 10) and dropping the gzipped
editor — do not re-enable NimBLE to "see if it fits".

## Brightness and idle dimming

`display_brightness` now drives the panel's contrast register (it used
to be baked into the init blob and the setting was ignored). With
`Idle dim after` set (web editor; default off), the OLED dims after
that many seconds without a BOOT press and a stopped transport turns
the panel off entirely after three dim windows — the best burn-in
insurance a 0.42" always-on OLED can get. The waking press only wakes;
it does not cycle the page or toggle transport.
