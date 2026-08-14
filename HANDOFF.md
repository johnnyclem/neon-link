# HANDOFF — Porting NEON LINK to the Daisy Seed (SSD1306/1309 display)

**From**: the agent that built the Teensy 4.1 target (PRs #21, #22 —
`teensy41/`, `docs/TEENSY41.md`)
**To**: the agent building the Electrosmith **Daisy Seed** target with an
**SSD1306/SSD1309 OLED**
**Date**: 2026-08-14

This document is everything I wish I had known before starting the Teensy
port, reshaped for your hardware. Read it end to end before writing code —
several of the traps below cost me a build-debug cycle each, and two of
them (§6.1, §6.2) produce firmware that *links fine and deadlocks on the
bench*.

---

## 1. Scope: what "the same task" means on your hardware

The Teensy parity list was: display + touch + encoders + PSRAM, then
Ableton Link, web editor, TRS MIDI, CLK/RST IN, audio. Your list is the
same *intent*, but the Daisy Seed changes which items are real:

| Capability | Teensy 4.1 outcome | Daisy Seed expectation |
|---|---|---|
| Display | 320×240 colour TFT — had to upscale + colourise the 128×128 mono UI | **Native fit.** The core `Framebuffer` is *already* in SSD1306 page layout (§4). Your flush is nearly a memcpy |
| Encoders | 2× EC11 via pin ISRs + portable `neon::QuadDecoder` | Same pattern; libDaisy also has an `Encoder` class but the portable decoder is host-tested — prefer it |
| Big RAM | 16 MB PSRAM (EXTMEM) — spot check + bump arena | 64 MB SDRAM — same idea, different init (libDaisy `sdram` section / `DSY_SDRAM_BSS`) |
| Ableton Link | **Ported** — native Ethernet + custom asio-free platform | **Not portable to stock hardware — no network interface at all** (§3). Use the internal-timeline architecture (§3.1) |
| Web editor | **Ported** — served over Ethernet | Not portable (no network). Document honestly, return-of-experience in §3.2 |
| WiFi / BLE | Impossible (no radio) — documented as ESP32-only | Same. Do not burn time on it |
| TRS MIDI out | Serial1 @ 31250, clock from a timer ISR | Same design on a Daisy UART (libDaisy `MidiUartTransport` or raw UART) |
| CLK/RST IN | Pin ISRs → portable `ExtClockEstimator` → drives the session | Same, except it drives the **internal timeline** directly (no Link session to steer) — this makes ext-clock *more* central on your target, it is the only external sync |
| Audio | Teensy Audio Library ISR + SGTL5000 shield, 44.1 kHz | **Better fit.** The Seed's codec runs 48 kHz — exactly `kSampleRate` in `main/audio_service.cpp`. libDaisy's audio callback is your render loop (§5) |
| Persistence | LittleFS in program flash (config + 4 presets) | QSPI flash — libDaisy `PersistentStorage` or raw QSPI + the same blob codec (§5.4) |

One hardware caveat to resolve on day one: "Daisy Seed 3" — pin down the
exact board revision and codec variant (early Seeds shipped WM8731, then
AK4556, later PCM3060). libDaisy's `DaisySeed::Init()` handles all of
them, but the codec matters if you touch gain staging. Also confirm your
panel: **SSD1306/1309 modules are 128×64**, not 128×128 — see §4.2, this
is your only real UI decision.

---

## 2. The architecture, as it actually is

Everything portable already exists and is host-tested. **Your job is glue,
not features.** The repo's layering (this held perfectly through the
Teensy port — I changed *zero* lines of portable code in PR #22):

```
components/neon_core/      ALL the logic. Menu model, screen renderer,
                           multi-clock engine, config codec (+JSON),
                           MIDI encoder/router, ext-clock estimator,
                           transport latch/tap tempo, audio DSP (click,
                           pulse-as-audio, mixer, SampleClock), quadrature
                           decoder. Plain C++17, no platform includes —
                           compile it in place, never copy it.
components/neon_hal/       Header-only interfaces (IPulseHw, ILinkSession,
                           IClockSource, ...).
components/app_state/      *Headers are portable, sources are ESP.* The
  include/app_state/       seqlock buses + queues + config-store API that
                           all services talk through. Implement the
                           headers for your target (see
                           teensy41/src/app_state_t41.cpp and
                           config_store_t41.cpp — ~300 lines total).
components/neon_hal_esp/,  ESP implementations. Read them as the reference
main/*.cpp                 for *semantics*; port the service loops almost
                           line-for-line (I did — link_service_t41.cpp is
                           main/link_service.cpp minus WiFi/AP).
teensy41/                  The target you should crib from. Same job as
                           yours, one PR earlier.
host/                      Host build + tests. Run them; they cover
                           everything you share with the other targets.
```

Key service loops to port (read the ESP original + my Teensy port side by
side; the Teensy versions are already "no-RTOS, polled from `loop()`",
which is closer to a libDaisy `while(1)` main than the FreeRTOS originals):

- `main/link_service.cpp` ↔ `teensy41/src/link_service_t41.cpp` — control
  queue (transport latch, tap, nudge/double/half, resync), ext-clock
  follow, snapshot publish. **On Daisy, replace the Link session with the
  internal timeline (§3.1) — the rest of the loop is unchanged.**
- `main/tasks_core1.cpp` ↔ `teensy41/src/pulse_hw_t41.cpp` +
  `service_engine()` in `teensy41/src/main.cpp` — edge generation
  cadence and the `hal::IPulseHw` emitter contract.
- `main/midi_service.cpp` ↔ `teensy41/src/midi_t41.cpp` — the 24 PPQN
  solve (`next_clock_tick_us`) is worth copying verbatim.
- `main/audio_service.cpp` ↔ `teensy41/src/audio_t41.cpp` — block render:
  SampleClock marks → block time window → `beat_window` → click render +
  `PulseRender::begin_block`/`render_channel` → `mix_block`.
- `components/oled_ui/src/oled_ui.cpp` — `assemble_status()` is the
  UiStatus recipe; `panel128.cpp` is the ESP's SSD1306-family flush
  (**read this before writing yours — it is almost exactly your display
  driver already**).

The one non-negotiable inherited from `HARDWARE.md` §4.3 / `SOFTWARE.md`:
**the pulse path is sacred.** Edges come from `MultiClockEngine::generate`
over contiguous half-open windows and are emitted by hardware-timer ISR,
never bit-banged from the main loop.

---

## 3. Ableton Link on the Daisy: read this before you try

The Teensy port's headline was running real upstream Link on bare metal.
The platform layer I built (`teensy41/link_platform/`) is genuinely
transport-agnostic — polled timers, single-threaded context, lightweight
IP types — **but the Daisy Seed has no network interface, so there is
nothing to bind a socket to.** No Ethernet MAC wired out, no radio. Do
not port Link to the stock Seed; there is no honest way to make it work.

### 3.1 What to build instead: the internal timeline

The Teensy target's *first* PR (#21) shipped without Link and had exactly
the architecture you want. Recover this file from git history:

```
git show 3934833:teensy41/src/internal_timeline.h
```

(`3934833` = "Add Teensy 4.1 touchscreen build target", the v1 commit; the
file was deleted in the parity PR when the real Link session replaced it.)

`InternalTimeline` is ~80 lines: it produces the same
`neon::TimelineSnapshot` Link produces — tempo as µs-per-beat Q32.32,
beat-anchored origin, transport flag — with tempo edits re-anchoring the
grid continuously and play restarting beat 0 at "now". Every consumer
(pulse engine, UI phase bar, MIDI clock, audio beat windows, ext-clock
follow) reads the snapshot and cannot tell the difference. Wire it where
`link_service_t41.cpp` calls `ablink::session()`:

- tempo commands → `set_tempo(nudge/clamp/double/halve/tap result)`
- transport latch fire → `set_playing()`
- ext-clock tempo update → `set_that_tempo`; RST IN phase request →
  re-anchor beat 0 at the captured edge time (on Teensy this called
  `session.request_beat_at_time`; internally it is
  `beat_at_origin = 0; origin = t_edge`)
- quantum changes → `set_quantum`

Keep the `hal::ILinkSession` seam anyway: implement it *over* the internal
timeline (a 60-line adapter) so the service loop stays line-identical to
the other targets, and so a future networked variant slots in.

### 3.2 If someone insists on Link later

The only credible path is USB gadget networking: STM32H750 USB device in
CDC-ECM/NCM mode + lwIP, so a host computer bridges the Seed onto the
LAN. That is a real project (weeks, not days). If it ever happens, the
Teensy platform layer is the starting point: `Runtime`/`Timer`/`Context`/
`Dispatcher`/`NetTypes`/`AsioTypes` shims are transport-free already —
only `Socket.hpp`, `link_runtime_t41.cpp` (the QNEthernet half), and
`ScanIpIfAddrs.hpp` would need lwIP equivalents. Same for the web editor
(`httpd_t41.cpp` is a plain non-blocking HTTP state machine over a
byte-stream client; it does not care what carries the bytes).

Until then: document both as "needs a network the hardware doesn't have",
the way `docs/TEENSY41.md` §1 documents WiFi/BLE. Honest capability
reporting is a house rule here — the ESP and Teensy REST/status surfaces
report missing features in JSON rather than dropping the keys.

---

## 4. The display: you have the easy one

### 4.1 The framebuffer is already in your panel's wire format

`neon::Framebuffer` (`components/neon_core/include/neon/gfx/
framebuffer.hpp`) is 128×128 mono in **SSD1306 page layout**: each byte is
8 vertical pixels, pages of 8 rows, column-major within a page. For an
SSD1306/1309 your flush is: set page/column address, then stream the
page's 128 bytes straight out of `fb.data()`. No pixel conversion, no
upscale LUT, no colour mapping — everything I built in
`teensy41/src/display_t41.cpp` exists only because that target had a
colour TFT. Read `components/oled_ui/src/panel128.cpp` (ESP): it drives
SSD1327/SH1107 from the same buffer and is your closest model. SSD1309 is
command-compatible with SSD1306 for everything this project needs (it is
the common 2.42" panel; init sequence differs in charge-pump handling —
SSD1309 modules usually run external VCC, so skip the 0x8D charge pump
command for those).

Transport choice: 4-wire SPI if you can spare the pins — a full 128×64
frame is 1 KB, ~0.3 ms at 30 MHz SPI vs ~25 ms over 400 kHz I2C. The I2C
number matters for §6.3.

### 4.2 The one real decision: 128×64 vs the 128×128 UI

SSD1306/1309 panels are **128×64**. The renderer draws 128×128. The
framebuffer header anticipates this ("smaller 128×64 panels can still be
driven by using the top half") but *do not accept that blindly* — look at
`neon/ui/theme_gen.hpp`: the live screen puts the hero BPM at y=22..52
(top half, fine) but the status row (y=66), identity/IP row (y=78), and
the phase bar (y=104..120) all live in the bottom half. A naive top-half
crop loses the phase bar and transport status — the two things a clock
module must show.

Your realistic options, in the order I would try them:

1. **Ship both halves, scrolled by context.** Show rows 0..63 normally;
   while playing, use the SSD1306's hardware display-start-line register
   (0x40 | line) to slide to the lower half (or flip on a timer). Cheap,
   zero core changes, mediocre UX.
2. **Vertical 2:1 downsample at flush time** (OR adjacent pixel rows into
   one panel row). ~20 lines in your flush loop; text becomes 5×3.5px —
   legible for the large fonts, marginal for `kSmall`. Prototype before
   committing.
3. **Add a 128×64 layout to the design system.** The honest fix:
   `design/tokens.json` → `scripts/gen_design.py` generates
   `theme_gen.hpp` layout constants; a second geometry (compact rows,
   bar at y=48) rendered by the same `render_ui`. This touches the
   portable core and the style-guide pipeline, so it needs the
   `neon_screens` golden fixtures regenerated (`./build-host/neon_screens >
   design/screens.json`) and `gen_design.py --check` green — see the
   `device-screens` and `design-system` CI jobs before you attempt it.
   Bigger job, correct outcome. If you go this way, do it as its own PR.

Whatever you pick, keep `render_ui()` pure and the golden tests passing —
the design-system CI jobs exist precisely to catch a target quietly
forking the UI.

---

## 5. Target skeleton: map of what you'll write

Mirror the `teensy41/` layout (`daisy/` or `daisyseed/`). Expected
inventory, with the Teensy file to crib from in parentheses:

- **Build**: libDaisy world usually means a Makefile against
  `libDaisy`/`DaisySP` submodules, but PlatformIO has
  `platform = ststm32`, `board = electrosmith_daisy`,
  `framework = libdaisy? / stm32cube` — **investigate early and pick
  whichever lets a `pio run`-style one-command CI job work**, because the
  CI pattern is already there (`.github/workflows/ci.yml`,
  `teensy41-build` job — copy it). Compile `neon_core` in place exactly
  like `teensy41/scripts/build_core.py` does (note §6.5 before you get
  clever with build scripts). No Link ⇒ **no exceptions needed ⇒ none of
  the `-fexceptions` scoping or `stdshim/` mutex machinery** — skip all
  of §6.4's mitigations, plain `-fno-exceptions` everywhere like stock.
- `include/board_pins_daisy.h` (`board_pins_t41.h`) — single source of
  truth, mirrored as a wiring table in your doc. Budget the pins before
  writing code (§6.6): OLED (SPI: SCK/MOSI/CS/DC/RST), 2 encoders (6
  pins), 6 pulse outs, CLK/RST IN, MIDI TX, 3 LEDs ≈ 20 GPIO — the Seed
  has 31, it fits, but the audio codec and boot pins are fixed; check
  the Seed's pinout for the SAI/USB reservations.
- `src/timebase_daisy.*` (`timebase_t41.*`) — 64-bit µs clock. libDaisy's
  `System::GetUs()` wraps a 32-bit TIM; build the same IRQ-guarded wrap
  extender. **Everything shares this one timebase** — engine, MIDI
  solve, ext-clock, sample clock.
- `src/pulse_hw_daisy.*` (`pulse_hw_t41.*`) — `hal::IPulseHw`: SPSC edge
  ring + timer ISR. The Teensy uses a dumb 100 µs tick (±100 µs edge
  placement, counted honestly via `late_max_us`/`late_avg_us` into
  status). The H750's TIMs can do better — a 1 MHz TIM with compare
  interrupt re-armed to the next edge gives ~µs placement for one extra
  page of code. Either is acceptable; keep the diagnostics counters.
- `src/app_state_daisy.cpp`, `src/config_store_daisy.cpp`
  (`app_state_t41.cpp`, `config_store_t41.cpp`) — the shared buses
  (seqlocks + IRQ-masked rings) and the `config_store.h` API over QSPI
  (libDaisy `PersistentStorage`, or raw QSPI sectors + the blob codec —
  the blob already carries magic/version/CRC, so a corrupt sector cleanly
  falls back to defaults). Keep the 2 s save debounce and the preset
  semantics (recall preserves network identity fields — inert here but
  keeps preset files portable across targets).
- `src/link_service_daisy.*` (`link_service_t41.*`) — the session/timeline
  service (§3.1). Keep the control-queue single-owner pattern: encoders,
  UI, and MIDI all `control_queue_push`; only this service touches the
  timeline.
- `src/clkin_daisy.*`, `src/midi_daisy.*`, `src/encoders_daisy.*` — near
  copies of the `_t41` versions (ISR capture ring / 24 PPQN ISR clock /
  `QuadDecoder` in pin-change ISRs + debounced buttons with 600 ms
  long-press).
- `src/oled_daisy.*` — §4. Also port `assemble_status()` from
  `oled_ui.cpp` / `main.cpp` (`active_net=0`, `ip=""`, `ble_on=false`,
  peers=0 — but wire `ext_clock` and `tempo_valid` for real).
- `src/audio_daisy.*` (`audio_t41.*`) — the good news section. libDaisy:
  `seed.SetAudioBlockSize(48)` (or 128), `StartAudio(callback)`; the
  callback is your `update()`: feed `SampleClock` marks (callback time +
  cumulative frames), stage-read the timeline (§6.1!), `beat_window`,
  click + pulse taps, `mix_block`, write the out buffer. 48 kHz matches
  the ESP audio service constants. `kDacLatencyUs` needs measuring on
  your codec path — start with 2×block and note it.
- `src/main.cpp` — libDaisy `while(1)` loop calling the polled services;
  same cadence discipline as `teensy41/src/main.cpp` (§6.3).
- `docs/DAISY.md` — model on `docs/TEENSY41.md`: what works / what
  doesn't and why, wiring tables mirroring the pin header, build/flash
  (`dfu-util` via USB DFU — document the BOOT button dance), design
  notes, ordered follow-ups. Add the README docs-table row and Getting
  Started section, and the CI job.

---

## 6. The traps (each of these cost me real time)

### 6.1 Never read a SeqLock from an ISR — single-core livelock

The `neon::SeqLock` buses are safe on the ESP because writer and reader
are on different cores. On a single core, **an ISR that spins on a
mid-publish seqlock waits forever for a writer that cannot run**. Your
audio callback and MIDI-clock ISR must get the timeline via an
**IRQ-masked staging copy** written from the main loop — see the `Staged`
struct in `audio_t41.cpp` and `g_tl` in `midi_t41.cpp`, and the comment
block in `audio_t41.h`. Main-loop readers may use the buses freely.
`irq_lock_t41.h` (PRIMASK save/restore, ISR-safe) is 20 portable lines —
copy it.

### 6.2 The engine horizon must outlast your slowest loop stall

The pulse ISR drains a ring the main loop refills. v1 shipped with a 15 ms
horizon and a ~30 ms blocking display push — the ring *starved mid-frame
while playing*. Rule: `horizon ≥ 2× worst main-loop stall`; measure the
stall, don't guess. Your OLED flush is 1 KB (≈0.3 ms SPI / ≈25 ms at
400 kHz I2C), plus QSPI config writes (erase+program can be tens of ms —
measure it, it is probably your worst stall). The ESP cadence (5 ms
refill / 15 ms horizon / 2 ms lead) is fine for SPI; keep the Teensy's
60 ms horizon if you use I2C or find slow QSPI writes. Emitters that must
not jitter (MIDI clock, audio) go in ISRs, full stop.

### 6.3 One timebase, everywhere

Every subsystem schedules in the same 64-bit µs domain. The moment two
clocks exist (HAL tick vs TIM µs vs audio frames), the beat math shears.
`SampleClock` exists precisely to servo audio frames onto the µs domain —
feed it marks from the audio callback and *only* trust
`us_at_frame`/`frame_at_us` for block windows.

### 6.4 Exceptions/`std::mutex` on arm-none-eabi (skip unless you port Link)

For the record, since you'll see the machinery in `teensy41/`: Ableton
Link throws/catches and uses `std::mutex`/`condition_variable`, which the
gthread-less toolchain lacks; and enabling `-fexceptions` broadly breaks
the Teensy link because FLASHMEM unwind entries can't span the ITCM↔flash
address distance (PREL31 overflow). The fixes were `stdshim/` (no-op
locks, sound because Link runs single-threaded) and scoping
`-fexceptions` to project TUs only. **None of this applies to a Linkless
Daisy build** — but if §3.2 ever happens, it all comes back; the STM32
flash lives at 0x0800_0000 and ITCM at 0x0000_0000, close enough that the
PREL31 issue may not bite there. Verify, don't assume.

### 6.5 Build-system honesty

Two PlatformIO lessons, in case you use it: (a) `env.Clone()` in a
`pre:` extra-script snapshots the environment **before the cross
toolchain is configured** — my cloned env silently compiled `neon_core`
with *host* gcc and the linker choked on x86-64 objects. Use the live
`env` for `BuildSources`, and `build_src_flags` for per-source flags.
(b) `build_flags` go to C *and* C++ — that's how `-fexceptions` leaked
into `startup.c` and caused the first PREL31 blowup. If you go the
Makefile route instead, none of this applies, but keep the "compile the
core in place, never vendor it" rule — it is what keeps three targets
from drifting.

### 6.6 Pin-budget against fixed peripherals before coding

I put the touch controller on pins 7/8 in v1; the audio shield needs
those exact pins for I2S. Cost: a wiring-table change, a doc change, and
would have cost a respin if hardware existed. On the Seed the fixed
consumers are the codec (internal), USB, QSPI (internal), and the SWD
pins — write the full pin map first and mirror it in the doc table.

### 6.7 CI is currently broken at the ACCOUNT level — don't chase ghosts

Every GitHub Actions run in this repo since Aug 13 "fails" in ~5 seconds
with zero steps executed and no runner assigned (`runner_id: 0`) — on
`main` pushes too. It is a billing/spending-limit condition, noted on
PRs #21/#22 and flagged to the owner. **Verify everything locally**
(clean target build with zero project-source warnings + `ctest` host
suite) and say so in the PR; do not interpret the instant red checks as
your failure, and do not skip adding your CI job because of it — it will
matter when Actions recovers.

### 6.8 Misc sharp edges

- Encoder direction is 50/50 — keep the `kInvert[]` escape hatch.
- `MenuModel` contract: rotate = focus/value, click = enter/confirm,
  long-press = universal back (`DESIGN_SYSTEM.md` §11). ENC2 = tempo ±1
  BPM, click = quantized start/stop *through the transport latch*, so
  play lands on the loop boundary.
- The menu edits a **working copy** of the config, re-synced only when
  `neon_config_rev()` moves and the user isn't mid-edit
  (`sync_ui_config()` in `teensy41/src/main.cpp`) — otherwise a preset
  recall mid-edit stomps the encoder.
- `UiStatus.anim_tick` comes from the timebase (`kIconTickHz`), not a
  frame counter — renderer purity is what keeps the golden screens
  meaningful.
- Config `display_brightness` maps to SSD1306 contrast (0x81, n) —
  cheap to support, already in the menu.
- Debounce QSPI/flash writes (2 s quiet period, `flush_now()` before
  reboot) — the ESP and Teensy stores both do; wear is real.

---

## 7. Definition of done

Match PR #21+#22's bar, adjusted for scope:

1. One-command build of the target from a clean checkout (document the
   exact commands; first-build downloads included).
2. Zero warnings from project sources on a clean build (third-party
   noise excluded; silence deliberate upstream warnings narrowly, the
   way `-Wno-multichar` is scoped in `teensy41/platformio.ini`).
3. Host suite untouched and green: `cmake -S host -B build-host … &&
   ctest`. If you took display option 3 (§4.2), golden fixtures
   regenerated and the design-system checks green.
4. Symbols sanity: `arm-none-eabi-nm -C firmware.elf` shows
   `render_ui`, `MultiClockEngine`, `MenuModel`, and your services.
5. `docs/DAISY.md` with wiring tables that mirror the pin header,
   honest "not on this hardware" section (network features), and an
   ordered follow-up list. README table row + Getting Started section.
   CI job added.
6. PR from a `claude/…` branch with local-verification evidence in the
   body, and the CI-outage caveat noted while it persists.

On-hardware validation to leave for the bench (list it in the PR): scope
CLK1 against the panel's phase bar, MIDI clock into a drum machine,
ext-clock lock from a square LFO into CLK IN, metronome click vs pulse
edges phase-aligned on a 2-channel scope.

## 8. Where everything is

- `teensy41/` — your closest sibling target (this port's output).
- `docs/TEENSY41.md` — the doc yours should rhyme with.
- `components/oled_ui/src/panel128.cpp` — the SSD1306-family flush.
- `git show 3934833:teensy41/src/internal_timeline.h` — the Linkless
  timeline (§3.1).
- `main/*.cpp` service loops — semantic reference for every service.
- `docs/AMYBOARD.md` — the other "constrained hardware, virtual pulse
  channels" precedent, useful if you run out of GPIO.
- PRs #21 and #22 — full history of this playbook actually being run,
  including the dead ends fixed in review of this document's §6.

Good luck. The core is solid, the seams are where I said they are, and
the panel you were handed is the one this UI was born for.
