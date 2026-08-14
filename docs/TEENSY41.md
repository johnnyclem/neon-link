# NEON LINK on Teensy 4.1 — Touchscreen Build Target

**Status**: Bring-up target (standalone clock; Link port is the tracked follow-up)
**Hardware**: PJRC Teensy 4.1 (i.MX RT1062 @ 600 MHz) with **16 MB PSRAM**
soldered to the underside pads, a **2.8" 320×240 SPI colour touchscreen**
(ILI9341 panel + XPT2046 resistive touch — the PJRC-standard module), and
**two EC11 rotary encoders** with push switches.

This target builds the same portable firmware core as the ESP32-S3 boards —
`components/neon_core` (menu model, screen renderer, multi-clock engine,
config codec) against the header-only `components/neon_hal` interfaces — from
a PlatformIO project in `teensy41/`. No sources are copied; the Teensy build
compiles the exact files the AMYboard firmware and the host test suite use,
so the screens, menu tree, and clock math cannot drift between platforms.

---

## 1. What works today

- **Full device UI on the colour panel.** `render_ui()` draws every screen
  into the 128×128 framebuffer; the TFT layer upscales it ×1.875 into a
  240×240 zone, colourised with the design tokens (neon cyan `#00F0FF` on
  near-black `#0B0C0F`, `design/tokens.json`).
- **Touch.** The remaining 80 px strip carries four buttons — `+`, `−`,
  `OK`, `BACK` — so the whole menu tree is drivable by touch alone
  (`+`/`−` auto-repeat when held; a tap anywhere on the UI zone is `OK`).
- **Two encoders.** ENC1 navigates/edits the menu (click = enter/confirm,
  hold = back). ENC2 is the performance encoder: rotate = tempo ±1 BPM,
  click = start/stop.
- **Pulse outputs.** The `MultiClockEngine` drives CLK1–4, RESET and RUN on
  a contiguous pin block from a 100 µs IntervalTimer edge emitter.
- **Tempo CV** as filtered PWM (config maps the BPM span, as on ESP32).
- **Config persistence** in emulated EEPROM using the same magic/CRC blob
  the ESP32 target keeps in NVS (survives reflash of the same layout).
- **PSRAM detection + spot check** at boot, reported on the serial console,
  with a bump-arena allocator (`psram::alloc`) reserved for the buffers
  that dwarf on-chip RAM (Link Audio jitter buffers are the intended
  tenant).

**Not ported yet**: WiFi/BLE (no radio on the Teensy), Ableton Link, web
editor, TRS MIDI, CLK/RST inputs, audio. The timeline is internal — tempo
and transport come from ENC2 — but it is published as the same
`TimelineSnapshot` the ESP32 firmware derives from Link, so the Link port
(over the Teensy 4.1's **native Ethernet** with QNEthernet) only replaces
the snapshot writer; the engine and UI are untouched.

---

## 2. Wiring

All pins are defined in one place: `teensy41/include/board_pins_t41.h`.
This table mirrors it.

### 2.8" ILI9341 display (SPI0)

| Display pin | Teensy 4.1 | Notes |
|-------------|-----------|-------|
| VCC         | 3.3 V     | |
| GND         | GND       | |
| CS          | **10**    | |
| RESET       | 3.3 V     | tied high (`kPinTftRst = 255`) |
| D/C         | **9**     | |
| SDI (MOSI)  | **11**    | |
| SCK         | **13**    | |
| LED         | **24**    | PWM backlight (`display_brightness`); tie to 3.3 V and set `kPinTftBacklight = -1` if unneeded |
| SDO (MISO)  | **12**    | |

### XPT2046 touch (same SPI bus)

| Touch pin | Teensy 4.1 |
|-----------|-----------|
| T_CS      | **8**     |
| T_IRQ     | **7**     |
| T_DIN     | 11 (MOSI) |
| T_DO      | 12 (MISO) |
| T_CLK     | 13 (SCK)  |

Raw calibration extents live at the top of `teensy41/src/touch_t41.cpp`.

### Rotary encoders (A/B/switch to GND, internal pullups)

| Function            | A | B | SW |
|---------------------|---|---|----|
| ENC1 (menu)         | 2 | 3 | 4  |
| ENC2 (tempo/transport) | 5 | 6 | 14 |

If an encoder counts backwards, flip its entry in `kInvert[]`
(`teensy41/src/encoders_t41.cpp`).

### Outputs and LEDs

| Function | Pin | Function | Pin |
|----------|-----|----------|-----|
| CLK1     | 33  | RESET    | 37  |
| CLK2     | 34  | RUN      | 38  |
| CLK3     | 35  | Tempo CV | 22 (PWM → RC filter) |
| CLK4     | 36  | LED Net / Beat / Run | 26 / 27 / 28 |

Outputs are **3.3 V logic** — level-shift to 5 V for Eurorack use
(HARDWARE.md §5.1 applies unchanged).

### Reserved (wired but not driven yet)

CLK IN = 30, RST IN = 31, TRS MIDI on Serial1 (TX = 1, RX = 0), I2C on
18/19.

---

## 3. Building and flashing

```bash
pip install platformio          # once
cd neon-link/teensy41
pio run                         # build
pio run -t upload               # flash (press the Teensy button if prompted)
pio device monitor              # 115200 baud
```

The first build downloads the Teensy platform + toolchain. The boot banner
reports the PSRAM check:

```
NEON LINK teensy41  psram=16MB test=ok
```

`psram=0MB` means the pads are unpopulated (the target still runs);
`test=FAIL` prints the first failing address — reflow the chips.

Host-side verification is unchanged and covers everything this target
shares with the ESP32 builds:

```bash
cmake -S host -B build-host -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host -j && ctest --test-dir build-host --output-on-failure
```

---

## 4. How the target is put together

```
teensy41/
  platformio.ini            board = teensy41, C++17, -Wall -Wextra
  scripts/build_core.py     compiles components/neon_core + cjson in place
  include/board_pins_t41.h  the pin map (single source of truth)
  src/
    main.cpp                Arduino setup/loop orchestration
    display_t41.*           ILI9341: 128x128 UI upscale + touch strip
    touch_t41.*             XPT2046: zones, buttons, auto-repeat
    encoders_t41.*          2x quadrature via neon::QuadDecoder (ISR)
    pulse_hw_t41.*          hal::IPulseHw on a 100 us IntervalTimer
    internal_timeline.h     standalone TimelineSnapshot writer
    config_store_t41.*      config blob <-> emulated EEPROM
    psram_t41.*             16 MB EXTMEM detection, spot check, arena
    timebase_t41.*          64-bit us clock (micros() wrap extender)
    irq_lock_t41.h          PRIMASK save/restore critical sections
```

The main loop keeps the ESP32 firmware's separation of concerns without
FreeRTOS: inputs every iteration, engine refill every 5 ms with a 15 ms
horizon and 2 ms lead (the exact cadence of the core-1 pulse task), UI at
~30 fps with a change-detected frame push.

### Timing honesty

The IntervalTimer emitter quantises edges to its 100 µs tick. That is fine
for bring-up and most clocking duties, but it is not the sub-microsecond
placement the ESP32 GPTimer path achieves. The tracked follow-up is a
FlexPWM/QuadTimer one-shot per channel, armed by the ISR, which brings
edge placement to hardware precision. `PulseHwT41::late_edges()` counts
edges that missed their slot by more than one tick.

### PSRAM

Teensyduino maps the soldered chips at `0x70000000` (EXTMEM) and sizes
them at startup (`external_psram_size`, in MB — 16 with both pads
populated). The boot check writes an address pattern at 64 KB strides
across the full range. `psram::alloc()` bump-allocates from
`extmem_malloc` and is where the Link Audio jitter buffers, config JSON
scratch, and any frame capture should live — RAM1/RAM2 stay reserved for
code, stacks, and the DMA frame buffer.

---

## 5. Follow-ups, in order

1. **Ableton Link over native Ethernet** (QNEthernet + the Link library's
   generic platform hooks) — replaces `InternalTimeline` as the snapshot
   writer; UiStatus grows a real `active_net`/`peers`/IP.
2. **FlexPWM one-shot edge emitter** for hardware-precision pulses.
3. **TRS MIDI out** on Serial1 (`midi_encoder` is already portable).
4. **CLK IN / RST IN capture** (pins reserved) → `ext_clock` path.
5. **Native 320×240 colour skin** for the UI, once a second `render_ui`
   surface earns its keep — until then the upscaled panel keeps the two
   surfaces pixel-identical with the OLED targets.
