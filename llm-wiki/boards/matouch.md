# Board: LinkSync MaTouch (ESP32-S3 1.28" round GC9A01)

`CONFIG_NEON_BOARD_LINKSYNC_MATOUCH` → implies `CONFIG_NEON_LINKSYNC=y`,
`CONFIG_NEON_AUDIO=n`. Makerfabs MaTouch ESP32-S3 1.28" Rotary (N16R8: 16 MB
flash, 8 MB octal PSRAM, native USB-C). Round 240×240 IPS on SPI, bezel
quadrature encoder, CST816 cap-touch, haptic/RTC/SD (not driven).

Face renderer: `main/matouch_service.cpp`. Pin map: `components/neon_board/include/board_pins.h`
(the `#elif CONFIG_NEON_BOARD_LINKSYNC_MATOUCH` block).

## What hardware is actually present
This is the crux for "which settings do anything" — see [honest-menu](../concepts/honest-menu.md).
- **Display + encoder + touch**: yes.
- **`kPulseVirtual = true`** → no Eurorack CLK/RESET/RUN jacks. The pulse
  engine runs but drives nothing physical.
- **No CV** (`kPinTempoCv = -1`), **no external clock in** (`kPinClkIn = -1`),
  **no Ethernet**.
- **No audio codec** — `NEON_AUDIO=n`; the whole audio engine is compiled
  out and `audio.enabled` is forced 0 at boot on link-sync.
- **BLE** is force-disabled at boot on link-sync (`ble_enabled=0`), and
  `midi_service` is compiled out (`#if !CONFIG_NEON_LINKSYNC`).

## Pins of note (as of 2026-08)
| Signal | GPIO | Notes |
|---|---|---|
| GC9A01 SPI | SCLK 42, MOSI 2, CS 1, DC 46, RES 21, BLK 45 (active HIGH) | |
| Encoder | A 48, B 47, push 17 (active-low) | PCNT ×4; 2 counts/detent |
| CST816 touch I2C | SDA 38, SCL 39 (INT 40, RST 18) | drives the settings panel |
| **MIDI TX** | **43** (U0TXD) | added this session — free because console is native USB Serial/JTAG; broken out as "TX". 24 PPQN TRS clock. |
| **MIDI RX** | **44** (U0RXD) | added this session — external MIDI-clock follow. |
| Haptic | 41 | not driven |

Console is **USB Serial/JTAG** (`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`,
secondary NONE), so classic UART0 pins 43/44 are free for UART1 MIDI.

## MaTouch-specific behaviour (this repo)
- **Live face**: phase ring (120 dots, r=108) + hero BPM + peers + RUN/STOP
  chip. When `playing && big_beat_display` it swaps to a **full-screen big
  beat number** (the pie/pendulum/pulse animations were prototyped then
  removed — too costly per frame on the round panel; only NUMBER remains).
- **Settings** are the shared `neon::MenuModel`, but trimmed to what this
  board can do via local visible-row maps in `matouch_service.cpp`
  (`kMenuVis`, `kMidiVis`, `kSysVis`). See [honest-menu](../concepts/honest-menu.md).
- **Touch**: drag to scroll the list; on a value row, tap **left half = −,
  right half = +** (bounded values like QUANTUM no longer stick at max).
- **MIDI**: clock **out** on GPIO43, external-clock **follow** on GPIO44,
  plus a "TEST NOTE" chip. See [midi-clock-path](../concepts/midi-clock-path.md).

## Build / flash
Isolated dir + sdkconfig so it doesn't clobber other S3 trees:
```
./scripts/flash_linksync-matouch.sh            # build + flash, auto-port
idf.py -B build-linksync-matouch -p <PORT> monitor
idf.py -B build-linksync-matouch -p <PORT> erase-flash   # wipe NVS (bad wifi cred, etc.)
```
`erase-flash` does **not** run in the normal flash script; a normal flash
leaves the `nvs` partition (0x9000) intact — so a bad stored value persists
across reflashes until you erase.

Related: [docs/LINKSYNC_MATOUCH.md](../../docs/LINKSYNC_MATOUCH.md) (human doc).
