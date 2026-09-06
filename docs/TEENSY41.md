# NEON LINK on Teensy 4.1 — Touchscreen Build Target

**Status**: Feature-parity target — Ableton Link over native Ethernet, web
editor, TRS MIDI clock, CLK/RST IN external clock, audio engine, touch UI
**Hardware**: PJRC Teensy 4.1 (i.MX RT1062 @ 600 MHz) with **16 MB PSRAM**
soldered to the underside pads, **native Ethernet** (PJRC MagJack kit on
the Ethernet header), a **2.8" 320×240 SPI colour touchscreen** (ILI9341 +
XPT2046 resistive touch — the PJRC-standard module), **two EC11 rotary
encoders**, TRS MIDI out, and (optionally) an **SGTL5000 audio shield**.

This target builds the same portable firmware core as the ESP32-S3 boards —
`components/neon_core` against the header-only `components/neon_hal`
interfaces, plus the shared `app_state` / `ablink` / config-store APIs —
from a PlatformIO project in `teensy41/`. No sources are copied; the Teensy
compiles the exact files the AMYboard firmware and the host test suite use,
so screens, menu tree, clock math, config codec, and the REST surface
cannot drift between platforms.

---

## 1. What works

- **Ableton Link over native Ethernet.** The real upstream Link library
  (`third_party/link`) runs on a custom `ableton::platforms::teensy41`
  platform layer (`teensy41/link_platform/`): QNEthernet UDP sockets,
  polled timers, and a single-threaded io context pumped from the main
  loop — no asio, no RTOS. Discovery, tempo/phase sync, start-stop sync,
  and peer counting all behave as on the ESP32 build. DHCP with a static
  fallback (`192.168.76.10` after 20 s without a lease), mDNS at
  `<device-name>.local`.
- **The web editor.** The committed gzipped single-file app is embedded in
  flash and served at `/`, with the `/api/*` REST surface the editor and
  the VST plugin speak: config get/put, status, transport, tempo (set /
  tap / nudge / double / half), resync, presets, reboot, factory reset.
  `/api/scan` returns `[]` (no radio), `/api/audio/channels` reports
  unavailable, `/api/ota` returns 501 (flash over USB).
- **TRS MIDI out** on Serial1: session-derived 24 PPQN clock from a 500 µs
  timer ISR (immune to UI stalls), Start/Stop on transport changes,
  honouring `midi_clock_out`, the clock policy, and the MIDI nudge.
- **CLK IN / RST IN**: rising-edge capture feeds the portable
  `ExtClockEstimator`; when the config allows (`clock_source` auto or
  external), the external tempo drives the Link session and RST IN anchors
  the downbeat — the same bidirectional path as the ESP32 firmware.
- **Audio engine** (with an SGTL5000 shield or any I2S DAC on the standard
  pins): the portable metronome click, pulse-as-audio taps (clock / reset /
  run roles), and mixer, rendered in the Teensy Audio Library's update ISR
  against the session grid via the portable `SampleClock`. Line-in tempo
  follow (`AUDIO > FOLLOW`) is off by default; the detector runs in the
  audio ISR and proposes `set_tempo` only.
- **Full device UI on the colour panel**: `render_ui()`'s 128×128 frame
  upscaled ×1.875 into a 240×240 zone (design-token colours), an 80 px
  touch strip (`+` / `−` / `OK` / `BACK`, auto-repeat), two encoders
  (ENC1 = menu, ENC2 = tempo ±1 BPM / quantized start-stop through the
  Link transport latch).
- **Pulse outputs** CLK1–4 / RESET / RUN from the 100 µs IntervalTimer
  edge emitter; **Tempo CV** as filtered PWM; **config + 4 presets**
  persisted in a LittleFS filesystem at the top of program flash (same
  magic/CRC blob as the ESP32's NVS store).
- **16 MB PSRAM** detection + spot check at boot, with a bump arena
  (`psram::alloc`) reserved for the big buffers.

### Not on this hardware / not ported yet

- **WiFi, the setup access point, and BLE MIDI** need a radio the Teensy
  4.1 does not have. They are permanently out of scope for the stock
  board; a co-processor (ESP32-C3 over UART) is the plausible path if a
  Teensy variant ever needs them. The editor and Link ride Ethernet here.
- **Link Audio streaming** (publish/subscribe): the clock-only
  `ableton::Link` is compiled in, not `ableton::LinkAudio`. The metronome
  and pulse audio work; network audio is the tracked follow-up.
- **Network OTA**: `/api/ota` returns 501 — flash over USB.
- **AMY synth**: ESP32-only component.

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
| T_CS      | **15**    |
| T_IRQ     | **16**    |
| T_DIN     | 11 (MOSI) |
| T_DO      | 12 (MISO) |
| T_CLK     | 13 (SCK)  |

Not 7/8 — those are I2S data once the audio shield is stacked. Raw
calibration extents live at the top of `teensy41/src/touch_t41.cpp`.

### Rotary encoders (A/B/switch to GND, internal pullups)

| Function               | A | B | SW |
|------------------------|---|---|----|
| ENC1 (menu)            | 2 | 3 | 4  |
| ENC2 (tempo/transport) | 5 | 6 | 14 |

If an encoder counts backwards, flip its entry in `kInvert[]`
(`teensy41/src/encoders_t41.cpp`).

### Outputs, inputs, LEDs, MIDI

| Function | Pin | Function | Pin |
|----------|-----|----------|-----|
| CLK1     | 33  | RESET    | 37  |
| CLK2     | 34  | RUN      | 38  |
| CLK3     | 35  | Tempo CV | 22 (PWM → RC filter) |
| CLK4     | 36  | CLK IN / RST IN | 30 / 31 |
| MIDI TX (Serial1) | 1 | LED Net / Beat / Run | 26 / 27 / 28 |

Outputs are **3.3 V logic** — level-shift to 5 V for Eurorack use
(HARDWARE.md §5 applies unchanged). Inputs need the usual series
resistor + clamp (§5.4).

### Reserved by the audio shield / Ethernet

I2S: 7 (OUT1A), 8 (IN1), 20 (LRCLK), 21 (BCLK), 23 (MCLK); codec I2C on
18/19. Native Ethernet uses the bottom-side header (PJRC MagJack kit) —
no GPIO cost.

---

## 3. Building and flashing

```bash
pip install platformio          # once
cd neon-link/teensy41
pio run                         # build (fetches QNEthernet on first run)
pio run -t upload               # flash (press the Teensy button if prompted)
pio device monitor              # 115200 baud
```

Boot banner:

```
NEON LINK teensy41  psram=16MB test=ok
link: session started
net: link=1 ip=192.168.1.23 (editor at http://neon-link.local/)
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
  platformio.ini            board = teensy41, C++17, QNEthernet
  scripts/build_core.py     compiles neon_core + cjson in place,
                            embeds the web bundle as a C array
  link_platform/            ableton::platforms::teensy41 — the asio-free
    ableton/discovery/AsioTypes.hpp    (shadow: lightweight IP types)
    ableton/platforms/Config.hpp       (shadow: platform selection)
    ableton/platforms/teensy41/...     Clock, Random, Timer, Socket,
                                       Context, Runtime, ScanIpIfAddrs
  stdshim/                  std::mutex / condition_variable for the
                            gthread-less toolchain (no-op locks: every
                            Link call runs on the one main thread)
  include/board_pins_t41.h  the pin map (single source of truth)
  src/
    main.cpp                service orchestration on the Arduino loop
    link_runtime_t41.cpp    QNEthernet half of the polled io runtime
    link_session_t41.cpp    hal::ILinkSession over ableton::Link
    link_service_t41.*      control queue, ext clock, snapshot publish
    net_t41.*               Ethernet DHCP/static + mDNS
    httpd_t41.*             web editor server + /api/* REST
    midi_t41.*              TRS MIDI clock (500 µs ISR) + transport
    clkin_t41.*             CLK/RST IN edge capture
    audio_t41.*             audio engine in the Audio-library ISR
    app_state_t41.cpp       the shared buses (seqlocks + IRQ-safe rings)
    config_store_t41.cpp    config + presets in LittleFS (program flash)
    display_t41.* touch_t41.* encoders_t41.*  (UI hardware)
    pulse_hw_t41.*          hal::IPulseHw on a 100 µs IntervalTimer
    psram_t41.* timebase_t41.* irq_lock_t41.h
```

Design notes worth knowing before editing:

- **Single-threaded by construction.** Every Link call, every handler,
  and every service runs on the main loop; only the emitters (pulse
  edges, MIDI clock, audio blocks) run in ISRs, and they communicate
  through IRQ-masked staging copies — never by reading the seqlock buses
  from interrupt context, which on a single core could livelock against
  a mid-publish main thread.
- **Exceptions are scoped.** Ableton Link throws/catches, so project TUs
  compile with `-fexceptions`; framework and library code keeps the
  platform's `-fno-exceptions` (their FLASHMEM functions would emit
  unwind entries that cannot span the ITCM↔flash address distance).
- **The engine horizon outlasts the display.** A full 240×240 SPI frame
  push blocks ~30 ms, so the pulse engine schedules 60 ms ahead with a
  15 ms refill: the ISR never starves mid-frame.
- **`/api` parity is deliberate.** The editor bundle is byte-identical
  to the one the ESP32 serves; missing capabilities are reported
  honestly in JSON rather than dropped from the surface.

---

## 5. Follow-ups, in order

1. **Link Audio streaming** (`ableton::LinkAudio`) — the jitter buffer,
   resampler, and frame ring are already portable; the streaming
   transport and the PSRAM-backed buffers are the work.
2. **FlexPWM one-shot edge emitter** for hardware-precision pulses
   (today's 100 µs tick is fine for clocking, not sample-accurate;
   `PulseHwT41::late_*()` counters quantify it in `/api/status`).
3. **MIDI in** on Serial1 RX (pin 0) → the portable router (notes to
   the synth voice / gates, transport, clock policy). This is also the
   gate on MIDI clock sync-in (docs/MIDI_PLL_PHASES_HANDOFF.md Phase C):
   the PLL, follower, and Daisy service wiring to copy all exist, but
   with no RX path there is nothing to feed them on this target yet.
4. **Network OTA** into the LittleFS region with a small bootloader
   handshake, so `/api/ota` stops returning 501.
5. **Native 320×240 colour skin** for the UI, once a second `render_ui`
   surface earns its keep — until then the upscaled panel keeps the two
   surfaces pixel-identical with the OLED targets.
