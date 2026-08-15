# NEON LINK on the Daisy Family — Seed OLED, Pod, patch.init()

**Status**: Standalone-clock target family — the full pulse engine,
TRS MIDI clock, CLK/RST IN external clock following, audio engine on the
built-in codec, Tempo CV on a true DAC, config + presets in QSPI flash.
No network features (no network interface on this hardware — see §1).
Three build configurations share the code in `daisy/`:

| Config | Board | Screen | Doc |
|---|---|---|---|
| `daisy/` | **Daisy Seed** + 128×64 SSD1306/1309 OLED, 2 encoders | yes | this file, §2-§7 |
| `daisy/pod/` | **Daisy Pod** (headless) | no | §8 |
| `daisy/patch_init/` | **patch.init()** (headless, Eurorack-native) | no | §8 |

**Hardware (Seed config)**: Electrosmith **Daisy Seed** (STM32H750 @
400 MHz, 64 MB SDRAM, 8 MB QSPI flash, stereo codec), a **128×64
SSD1306 or SSD1309 OLED** on SPI, **two EC11 rotary encoders**, six
pulse outputs, CLK/RST IN, TRS MIDI out, and three status LEDs.

This target builds the same portable firmware core as the ESP32-S3 and
Teensy 4.1 targets — `components/neon_core` against the header-only
`components/neon_hal` interfaces, plus the shared `app_state` /
config-store APIs — from a Makefile project in `daisy/` against the
`third_party/libDaisy` submodule (pinned at v8.1.0). No sources are
copied; the Seed compiles the exact files the other targets and the host
test suite use, so screens, menu tree, clock math, and the config codec
cannot drift between platforms.

Board revisions: `DaisySeed::Init()` auto-detects the codec variant
(AK4556 on the original Seed, WM8731 on 1.1, PCM3060 on the Seed 2 DFM)
and needs no configuration. A newer Seed revision than libDaisy v8.1.0
knows about means bumping the submodule, nothing here.

---

## 1. What works

- **The full pulse engine.** CLK1–4 / RESET / RUN from a TIM5
  compare-interrupt emitter: the timer free-runs at 1 MHz and a compare
  channel is re-armed to the exact microsecond of each pending edge, so
  placement error is interrupt latency (~1–2 µs) rather than a polled
  tick's ±100 µs. A second compare channel keeps the 10 kHz
  housekeeping tick (timebase wrap service, input sampling). The honest
  lateness diagnostics remain (`late_max_us` / `late_avg_us`, with
  "late" now meaning >10 µs).
- **The internal timeline.** With no Link session to join, the module
  runs its own beat grid (`daisy/src/internal_timeline.h`, recovered
  from the Teensy target's pre-Link build): tempo edits re-anchor the
  grid continuously, play restarts beat 0 at "now", and every consumer —
  pulse engine, UI phase bar, MIDI clock, audio beat windows — reads the
  same `TimelineSnapshot` it would get from Link. The service loop
  (`link_service_daisy.cpp`) is line-for-line the Teensy Link service;
  the session behind the `hal::ILinkSession` seam is a 60-line adapter
  over the internal timeline, so a future networked variant slots in.
- **CLK IN / RST IN.** Rising edges (sampled at 10 kHz, ±100 µs) feed
  the portable `ExtClockEstimator`; when the config allows
  (`clock_source` auto or external), the external tempo drives the
  timeline and RST IN anchors the downbeat. **This is the only external
  sync the target has**, which makes it more central here than on any
  other build — it is how the Seed joins someone else's clock.
- **TRS MIDI out**: session-derived 24 PPQN clock from a 500 µs TIM4
  ISR (immune to UI and QSPI stalls), Start/Stop on transport changes,
  honouring `midi_clock_out`, the clock policy, and the MIDI nudge.
- **TRS MIDI in** through the portable `SerialMidiParser` →
  `MidiRouter` — the same router the ESP32 runs for BLE MIDI: notes
  gate a pulse output (applied immediately at the pin), pitch maps to
  the Tempo CV jack (1 V/oct) when enabled, CCs edit latency/shuffle,
  Start/Stop drives the transport, Program Change recalls presets, and
  the incoming clock stream can be forwarded to the TRS output per the
  clock policy. Independently, incoming **MIDI clock is a second
  external tempo source** (fixed 24 PPQN) feeding its own
  `ExtClockEstimator`; the CLK IN jack outranks it when both are alive,
  and phase anchoring stays RST IN's alone (MIDI has no downbeat
  message — Start restarts beat 0 through the transport instead).
- **Audio engine on the built-in codec** — the best-fit subsystem: the
  codec runs 48 kHz, exactly the rate the portable audio services are
  written for. Metronome click, pulse-as-audio taps (clock / reset / run
  roles), and the mixer render inside libDaisy's audio callback against
  the session grid via the portable `SampleClock`.
- **Device UI on the 128×64 panel**: the design system's native compact
  layout (`ui::kLayout64`), drawn pixel-perfect by the same pure
  `render_ui()` as every other target (§4). ENC1 = menu
  (rotate/click/hold-for-back), ENC2 = tempo ±1 BPM, click = quantized
  start/stop through the transport latch. `display_brightness` maps to
  the contrast register.
- **Tempo CV** from the STM32H7's true 12-bit DAC (no PWM filter
  needed); **config + 4 presets** in raw QSPI sectors using the same
  magic/CRC blob codec as the ESP32's NVS and the Teensy's LittleFS
  stores, with the same 2 s debounce and preset-recall semantics.
- **64 MB SDRAM** initialized by `DaisySeed::Init()` (nothing uses it
  yet — it is headroom for the audio follow-ups).

### Not on this hardware

- **Ableton Link and the web editor** need a network interface the
  stock Seed does not have — no Ethernet MAC wired out, no radio. This
  is a hardware fact, not a porting gap: the Teensy target's
  transport-agnostic Link platform layer exists, but there is nothing
  on this board to bind a socket to. The only credible future path is
  USB gadget networking (CDC-NCM + lwIP, so a host computer bridges the
  Seed onto the LAN) — a real project, tracked in §7, not promised.
- **WiFi, the setup access point, and BLE MIDI**: no radio. Permanently
  out of scope for the stock board, as on the Teensy.
- **Network OTA**: flash over USB DFU (§3).
- **AMY synth / Link Audio**: ESP32-only components.

The house rule from the other targets applies: these are reported
honestly (the UI shows no network row values, `active_net = 0`) rather
than pretended at.

---

## 2. Wiring

All pins are defined in one place per board:
`daisy/include/board_pins_seed.h` (this section; the headless boards'
maps are in §8, dispatched through `board_pins_daisy.h`). These tables
mirror it. Pin numbers are Daisy Seed "D" GPIO names (silkscreen 1–40
maps to D0–D30 per the Electrosmith pinout card).

### 128×64 SSD1306/SSD1309 OLED (SPI1, 4-wire)

| Display pin | Daisy Seed | Notes |
|-------------|-----------|-------|
| VCC         | 3V3       | |
| GND         | GND       | |
| CS          | **D7**    | software CS |
| SCK / D0    | **D8**    | SPI1 SCK |
| D/C         | **D9**    | SPI1 MISO repurposed — the panel is write-only |
| SDA / D1 (MOSI) | **D10** | SPI1 MOSI |
| RES         | **D11**   | hardware reset pulse at boot |

I2C-only modules work in principle (the FB is already in SSD1306 page
layout) but are not wired here: a full frame is ~25 ms at 400 kHz vs
~1.4 ms on SPI, which would force the Teensy's longer engine horizon.
Prefer the SPI variant of the module.

SSD1309 modules running external VCC: set `kOledExternalVcc = true` in
`board_pins_seed.h` to skip the SSD1306 charge-pump command.

### Rotary encoders (A/B/switch to GND, internal pullups)

| Function               | A  | B  | SW |
|------------------------|----|----|----|
| ENC1 (menu)            | D0 | D1 | D2 |
| ENC2 (tempo/transport) | D3 | D4 | D5 |

If an encoder counts backwards, flip its entry in `kInvert[]`
(`daisy/src/encoders_daisy.cpp`).

### Outputs, inputs, LEDs, MIDI

| Function | Pin | Function | Pin |
|----------|-----|----------|-----|
| CLK1     | D15 | RESET    | D19 |
| CLK2     | D16 | RUN      | D20 |
| CLK3     | D17 | Tempo CV | D23 (DAC1 → op-amp to 0–5 V) |
| CLK4     | D18 | CLK IN / RST IN | D21 / D22 |
| MIDI TX / RX (USART1) | D13 / D14 | LED Beat / Run / Ext | D24 / D25 / D26 |

Free for expansion: D6, D12, D27, D28, D29, D30 (D29/D30 double as USB
HS on the header — leave them last).

Outputs are **3.3 V logic** — level-shift to 5 V for Eurorack use
(HARDWARE.md §5 applies unchanged). Inputs need the usual series
resistor + clamp (§5.4). The Ext LED lights while the module is locked
to CLK IN (it replaces the Teensy's Net LED — nothing to report there).

### Fixed / internal

Audio I/O uses the Seed's dedicated codec pins (pins 16–25 on the
physical header — not D-numbered GPIO). QSPI flash, SDRAM, USB micro,
and SWD are internal to the SOM.

---

## 3. Building and flashing

```bash
sudo apt-get install gcc-arm-none-eabi dfu-util   # once (or the xPack toolchain)
cd neon-link
git submodule update --init --recursive           # libDaisy + its ST drivers
make -C third_party/libDaisy -j                   # once: libdaisy.a
make -C daisy -j                                  # Seed OLED build
make -C daisy/pod -j                              # Daisy Pod headless (§8)
make -C daisy/patch_init -j                       # patch.init() headless (§8)
```

Each configuration builds into its own directory
(`daisy/build/neon-link-daisy.bin`, `daisy/pod/build/neon-link-pod.bin`,
`daisy/patch_init/build/neon-link-patch-init.bin`), so switching boards
never mixes object files.

Flashing over USB DFU: hold the Seed's **BOOT** button, tap **RESET**,
release BOOT (the Seed enumerates as an STM DFU device), then:

```bash
make -C daisy program-dfu           # or -C daisy/pod / -C daisy/patch_init
```

The image runs from internal flash (`APP_TYPE = BOOT_NONE`) and
currently uses ~118 KB of the 128 KB sector at `-Os`. If it ever
outgrows that, the escape hatch is the Daisy bootloader
(`APP_TYPE = BOOT_QSPI`) — but that executes from the QSPI chip the
config store erases at runtime, so the store must move first
(`daisy/src/config_store_daisy.cpp` explains the constraint).

Host-side verification is unchanged and covers everything this target
shares with the other builds:

```bash
cmake -S host -B build-host -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host -j && ctest --test-dir build-host --output-on-failure
```

Linker noise: newlib's six `_close/_fstat/_isatty/_lseek/_read/_write is
not implemented` warnings come with libDaisy's stock `nosys.specs` link
and appear in every Daisy build; project sources compile with
`-Wall -Wextra` and zero warnings.

---

## 4. The display: the native 128×64 layout

The portable `neon::Framebuffer` is already in SSD1306 page layout
(1 byte = 8 vertical pixels, pages of 8 rows), so the flush is a
page-window set plus a 1 KB stream — no pixel conversion. The geometry
question — the renderer's screens were born 128×128, the panel is
128×64 — is answered by the design system itself
(DESIGN_SYSTEM.md §5): `design/tokens.json` carries a second vertical
flow (`device_compact`), `gen_design.py` emits it as `ui::kLayout64`,
and the same pure `render_ui()` draws it. Hero BPM, status row, and
phase bar are purpose-set for 64 rows; the unit label and identity row
are the two bands the compact flow drops (nothing on this hardware has
a network identity to show); lists keep their 12 px pitch at four rows.

The compact flow renders into the **top half** of the shared 128×128
framebuffer — rows 64..127 provably blank (the host suite asserts it
for every screen) — so the panel flush is FB pages 0..7 verbatim. Both
geometries of every fixture are committed in `design/screens.json` and
shown side by side in the web style guide.

`daisy/src/oled_daisy.h` keeps three modes behind `kDisplayMode`:

- **`kNative`** (default): the compact layout, pixel-perfect.
- **`kDownsample`**: the full 128×128 layout OR'd 2:1 at flush time
  (256-byte LUT). Text is half height; kept for anyone who prefers the
  big layout's information density (identity row, unit label).
- **`kTopHalf`**: rows 0..63 of the 128×128 layout. A bring-up
  diagnostic.

---

## 5. How the target is put together

Mirrors `teensy41/` (read that target's doc §4 for the pattern; the
Daisy versions differ only where the silicon does):

- **`main.cpp`** — bare-metal `while(1)` loop polling the services;
  15 ms engine refill with a 60 ms horizon, ~30 fps UI. The FreeRTOS
  tasks of the ESP build and the Arduino loop of the Teensy build
  become the same polled services here.
- **`timebase_daisy`** — 64-bit µs clock over libDaisy's 32-bit TIM2
  tick (wraps every ~21 s; the extender runs in the pulse ISR, and the
  µs conversion divides extended ticks so no error accumulates).
  Everything schedules in this one domain — engine, MIDI solve,
  ext-clock capture, `SampleClock` marks.
- **`pulse_hw_daisy`** — `hal::IPulseHw`: SPSC edge ring, TIM5
  free-running at 1 MHz. Compare channel 1 is re-armed to the exact
  microsecond of the next pending edge (placement error = interrupt
  latency at NVIC priority 4); compare channel 2 is the 10 kHz
  housekeeping tick that services the timebase wrap extender, samples
  the encoders and CLK/RST IN (libDaisy has no EXTI wrapper; 10 kHz
  oversampling lands capture jitter well inside the estimator's median
  filter), and arms channel 1 for whatever falls due next — the ≥2 ms
  scheduling lead means the producer never touches the timer. MIDI note
  gates bypass the ordered ring entirely (`set_level_now`).
- **`app_state_daisy` / `config_store_daisy`** — the shared seqlock
  buses + IRQ-masked rings, and the `config_store.h` API over raw QSPI
  sectors (config + 4 presets at the top of the 8 MB chip, one 4 KB
  sector each). The blob's magic/version/CRC means a corrupt or blank
  sector cleanly falls back to defaults.
- **`link_service_daisy` + `link_session_daisy` + `internal_timeline`**
  — §1. Control-queue single-owner pattern: encoders and (future) MIDI
  all `control_queue_push`; only this service touches the timeline.
- **`audio_daisy`** — libDaisy's non-interleaved float callback at
  48 kHz / 48-frame blocks: `SampleClock` marks → block time window →
  `beat_window` → click render + `PulseRender` taps → `mix_block`
  straight into the wire buffers. `kDacLatencyUs` is provisionally two
  block periods (2 ms) — measure on the bench (§7).
- **Interrupt ranking** (lower = higher priority): pulse emitter TIM5
  at 4, MIDI tick TIM4 at 6, audio SAI DMA re-ranked from libDaisy's 0
  to 8 — the same ordering as the Teensy, so a long audio block can
  never delay a pulse edge.

Two single-core rules inherited from the Teensy port (its doc §4, this
repo's HANDOFF §6 — both learned the hard way):

1. **Never read a SeqLock from an ISR.** Writer and reader share the
   core; an ISR spinning on a mid-publish seqlock waits forever. The
   audio callback and MIDI tick read IRQ-masked staging copies written
   by their `poll()` functions.
2. **The engine horizon must outlast the worst main-loop stall.** The
   OLED flush is ~1.4 ms (SPI), so the steady-state 60 ms horizon is
   comfortable. The one stall it cannot cover is the QSPI config-store
   sector erase (typ. ~45 ms, worst-case hundreds of ms), so the store
   calls a pre-persist hook that tops the schedule up by 800 ms first —
   the edge ring (1024 entries) holds that much at any sane clock
   configuration. Pathological configs (all four outputs at maximum
   PPQN × multiplier) can exceed the ring during a save; a config save
   is a user edit, so the trade is acceptable and documented here.

---

## 6. On-hardware validation (bench list)

CI proves the build; these need a Seed and a scope:

1. CLK1 against the panel's phase bar (alignment and the quantized
   start latch landing on the loop boundary).
2. MIDI clock into a drum machine; verify tempo and Start/Stop.
3. Ext-clock lock from a square LFO into CLK IN (tempo follow, RST IN
   downbeat anchor, Ext LED).
4. Metronome click vs pulse edges phase-aligned on a 2-channel scope;
   measure the true `kDacLatencyUs` and update `audio_daisy.cpp`.
5. A config save mid-playback (watch for pulse disturbance during the
   QSPI erase; confirm the pre-persist top-up covers it).
6. `display_brightness` sweep, including 0 = panel blanked.

---

## 7. Follow-ups, in order

1. ~~Native 128×64 layout in the design system~~ — **done** (§4):
   `device_compact` tokens → `ui::kLayout64` → the same `render_ui()`,
   with both geometries in the golden fixtures and the style guide.
2. **Measure `kDacLatencyUs`** on the codec path and pin it (§6.4).
3. ~~µs pulse placement~~ — **done** (§1, §5): TIM5 free-runs at 1 MHz
   with a compare channel re-armed to each edge's exact microsecond; a
   second compare keeps the 10 kHz housekeeping tick.
4. ~~MIDI in~~ — **done** (§1): the portable `SerialMidiParser` (new,
   host-tested) feeds the same `MidiRouter` the ESP32 runs, plus MIDI
   clock-follow as a second external tempo source. Seed: USART1 RX
   (D14); Pod: its own TRS MIDI IN jack; patch.init(): the A2 header
   pin.
5. **USB gadget networking** (CDC-NCM + lwIP) if Link on this hardware
   ever becomes real — the Teensy platform layer
   (`teensy41/link_platform/`) is transport-agnostic and is the
   starting point; only its socket layer and runtime pump are
   QNEthernet-specific. Same path would carry the web editor. Weeks,
   not days; do not start it casually.
6. **Use the SDRAM**: longer audio buffers / Link Audio jitter buffers
   land here for free once networking exists.


---

## 8. Headless boards: Daisy Pod and patch.init()

Two screenless configurations reuse everything in `daisy/src` except
the menu/display path: `main_headless.cpp` swaps `render_ui()` + the
OLED flush for a per-board `controls_*.cpp` that pushes the same
control-queue commands the encoders push on the Seed build. The link
service, pulse engine, CLK/RST IN follow, TRS MIDI clock, audio engine,
and QSPI config store are byte-for-byte the same services.

**On "controlled via the web interface / VST":** not on this hardware.
The web editor and the VST plugin speak to the `/api` REST surface that
the ESP32 and Teensy targets serve over their networks — the Daisy has
no network interface to serve it on, which is the same hardware fact
that rules out Ableton Link (§1). Until USB gadget networking exists
(§7 follow-up 5), a headless Daisy is configured by its panel controls,
CLK/RST IN, and presets: save a preset on a networked target (or the
Seed OLED build), and the shared blob codec recalls it here. What the
headless panels *can* do live is tempo, transport, tap, resync, and
clock-source selection — the performance surface.

### Daisy Pod (`daisy/pod/`)

The Pod's own controls carry the performance surface; clock I/O rides
the free Seed GPIO on the expansion headers
(`daisy/include/board_pins_pod.h`):

| Control | Function |
|---|---|
| Encoder rotate | tempo ±1 BPM |
| Encoder click | start/stop (quantized, through the transport latch) |
| Button 1 | tap tempo |
| Button 2 | resync at next loop |
| LED 1 | beat flash (white) while playing; dim red when stopped |
| LED 2 | green = RUN gate live; amber = locked to CLK IN |
| Knobs 1/2 | unmapped (follow-up) |

| Function | Pin | Function | Pin |
|----------|-----|----------|-----|
| CLK1     | D0  | RESET    | D10 |
| CLK2     | D7  | RUN      | D16 |
| CLK3     | D8  | Tempo CV | D22 (DAC2 → op-amp to 0-5 V) |
| CLK4     | D9  | CLK IN / RST IN | D29 / D30 |
| MIDI TX (UART4) | D12 | MIDI IN | the Pod's own TRS jack |

The Pod's encoder click owns D13 (USART1 TX), so TRS MIDI OUT moves to
UART4 on D12; the Pod's own TRS **MIDI IN** jack (USART1 RX, D14) feeds
the router and the MIDI clock-follow (§1) through a separate RX-only
UART init that never touches the encoder's pin. D29/D30 double as USB
HS — reclaim
them first if USB HS is ever wired. The microSD slot (D1-D6) is
untouched.

### patch.init() (`daisy/patch_init/`)

Eurorack-native: the module's own jacks are the clock I/O, with proper
0-5 V levels both directions and no external conditioning
(`daisy/include/board_pins_patch_init.h`):

| Panel | Function |
|---|---|
| GATE OUT 1 / 2 | CLK1 / RESET |
| GATE IN 1 / 2 | CLK IN / RST IN (inverting input stage handled in software) |
| CV OUT 2 | Tempo CV — the module's real 0-5 V output stage |
| Button (B7) | start/stop (quantized); hold = resync at next loop |
| Toggle (B8) | up = auto-follow CLK IN, down = internal clock only |
| Knob 1 (CV_1) | tempo 20-300 BPM, soft-pickup (inert until moved) |
| Panel LED | beat flash while playing |
| TRS MIDI OUT / IN | the A-header "UART1" pins (A3 TX, A2 RX — UART4 on the STM32) |

Only two gate jacks exist, so CLK2-4 and RUN have no physical pin — the
virtual channels still run, and the **audio outputs can carry
clock/reset/run as pulse-as-audio roles** (the audio engine's
`role_l`/`role_r` config), which is the intended way to get more clock
outputs from this panel. CV OUT 1 is left free (it drives the panel LED
on stock hardware); CV ins 2-8 and the remaining ADCs are unmapped
follow-ups.

### Headless bench list (on top of §6)

1. Pod: encoder direction (`kInvert[]`), tap-tempo feel, LED colors.
2. patch.init(): GATE IN polarity against a real gate source (the
   inverting stage), knob-pickup behavior across a power cycle, CV OUT 2
   tempo scaling against a voltmeter.
3. Both: preset recall from a blob saved on another target.
