# NEON LINK Firmware Architecture

This document records the firmware architecture decisions locked at the
start of implementation. It is the companion to [SOFTWARE.md](../SOFTWARE.md)
(the requirements handoff): SOFTWARE.md says *what*, this file says *how*.

## Build targets

The tree builds two ways:

| Build | Toolchain | What it proves |
|-------|-----------|----------------|
| Firmware | ESP-IDF **v5.3.x** (pinned in CI), target `esp32s3` | The real application compiles for the ESP32-S3-WROOM-1 |
| Host | Any desktop gcc/clang, CMake ≥ 3.16 | Portable core logic is correct (unit tests, ASan/UBSan) |

```
# Host tests (no ESP-IDF needed)
cmake -S host -B build-host -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host -j
ctest --test-dir build-host --output-on-failure

# Firmware (with ESP-IDF v5.3.x exported)
idf.py set-target esp32s3
idf.py build flash monitor
```

Both run in CI on every PR (`.github/workflows/ci.yml`).

## Layering

```
main/                     app wiring: boot, task creation, pin map
components/
  neon_core/              PORTABLE logic (C++17 stdlib only, no IDF includes)
  neon_hal/               PORTABLE hardware interface definitions
  neon_hal_esp/           ESP-IDF implementations of neon_hal
  (later) ableton_link/, net_manager/, ble_midi/, oled_ui/, web_ui/
host/                     host-native build: unit tests, fakes, neon_sim
third_party/              vendored deps (doctest; Ableton Link submodule later)
```

**Portability rule**: `neon_core` and `neon_hal` may include only the C++17
standard library and each other. The host CI job compiles them with
`-Wall -Wextra -Werror` plus ASan/UBSan, so an accidental IDF include or UB
fails the build. All timing math, schedulers, parsers, state machines, and
config logic live here so they can be tested without hardware.

## Dual-core split (SOFTWARE.md §5)

- **Core 0** — networking/application: WiFi, Ethernet, Ableton Link service
  task, BLE, web server, OLED, encoder. The ESP-IDF main task and esp_timer
  callbacks are pinned here (`sdkconfig.defaults`).
- **Core 1** — real-time: the pulse task and the GPTimer alarm ISR. Nothing
  else is scheduled at its priority on this core. The audio render task
  (`CONFIG_NEON_AUDIO`) also lives here, four priorities down: I2S DMA
  gives it ~11 ms of slack, and core 0's WiFi/asio/httpd bursts are the
  worse neighbour to have.

## Pulse engine

### GPTimer, not RMT

The ESP32-S3 has 4 RMT TX channels, but the module drives at least six
edge-accurate outputs (CLK1–4, RESET, RUN) plus the Beat LED — and RUN is a
level, not a waveform. RMT also wants pre-rendered symbol buffers, which fit
a continuously-retimed musical grid poorly (every tempo/shuffle change would
force a re-render race). Instead:

- One **GPTimer at 1 MHz** (1 µs resolution). Alarm is always armed at the
  earliest pending edge across all outputs.
- The ISR (IRAM-resident, core 1) writes `GPIO_OUT_W1TS_REG` /
  `GPIO_OUT_W1TC_REG` directly — all pulse outputs are on GPIO < 32 so one
  register write flips any combination of them simultaneously.
- Jitter is interrupt latency only: ~1–2 µs with core 1 kept clear, well
  inside the sub-millisecond musical budget. This satisfies the "hardware
  timers or RMT only" requirement (HARDWARE.md §4.3): the timer *is* the
  hardware; software only decides which edges it emits.

### Timebase correlation

Ableton Link's ESP32 platform clock is `esp_timer_get_time()`. The GPTimer
count is correlated to esp_timer once at init; both derive from the same
crystal so the offset is constant. The whole pipeline — Link session state,
engine edge times, hardware alarms — therefore shares one microsecond
domain with zero drift between stages.

### Producer/consumer contract

The core-1 pulse task refills a lock-free SPSC ring every 5 ms with all
edges inside a 15 ms horizon, at least 2 ms ahead of real time. When the
ring empties the ISR parks at a 1 ms poll, so a freshly submitted edge is
always noticed within 1 ms — inside the 2 ms lead. No locks are shared
with the ISR.

### Integer beat math

Doubles exist only at the Link boundary (core 0). The engine carries tempo
as **microseconds-per-beat in Q32.32** and advances each output's grid with
a quotient/remainder accumulator. An output's rate is the rational
`p/q = (ppqn × mult) / div` pulses-per-beat, and the per-tick period
`mpb·q/p` is kept as `(quotient, remainder over p)` so that every `p`
ticks sum to *exactly* `q` beats — the grid cannot drift from the session
grid no matter how long the set runs. Host tests assert this bit-exactly
(`host/tests/test_pulse_channel.cpp`).

The ESP32-S3 FPU is single-precision only; doubles are software-emulated
and are banned from anything reachable by the pulse path.

### Output engine (milestone 3, roles added in the parity pass)

`neon::MultiClockEngine` owns six merged, time-ordered channels:

- **OUT 1–4** — independent `PulseChannel`s: per-output PPQN/mult/div,
  trigger or duty-cycle square pulse shape, and shuffle (odd ticks delayed
  by a percentage of the period; parity is derived from the absolute tick
  index so swing feel survives re-anchors). Defaults: 4/2/1/24 PPQN.
- **RESET** — `kStartOfPlay` (one pulse when the transport starts — the
  HARDWARE.md "Reset / Start pulse" default), `kEveryBar` (a 1/quantum
  rational channel while playing), `kAtStop`, or off.
- **RUN** — gate mirroring the session transport.

Each of OUT 1–4 carries a **role**, so a jack is not locked to being a
clock: `kClock`, `kGate` (high while playing), `kResetLoop` (a trigger on
every loop boundary — internally a 1/quantum rational channel),
`kResetStart`, `kResetStop`. Non-clock roles produce no periodic edges;
their pulses come from the transport-transition one-shot queue, which also
carries the RUN and RESET one-shots so everything merges through one
time-ordered path.

Latency compensation is a signed µs offset applied uniformly to every
emitted edge. Reset-role channels and the RESET jack take an additional
negative offset when `reset_before_edge` is set, so a sequencer latches
the reset just before the clock that starts the loop. Clocks free-run on
the beat grid by default; `transport_gating` optionally stops them with
the transport, and a per-output `free_run` overrides that for one jack
(the "Clock (Always On)" idiom, paired with a `kGate` output for DIN-Sync
style clocking).

### Transport control (`neon/transport.hpp`)

Portable, host-tested, and the only place beat↔time conversion lives
outside the pulse channel:

- `beat_at_q32` / `time_at_beat_q32` / `next_loop_boundary_us` — the loop
  grid, used for quantized transport, resync, and the editor's loop meter.
- `TapTempo` — averages a run of taps, restarting the run on a gap or an
  out-of-range interval so a stray tap cannot poison the tempo.
- `TransportLatch` — arms a play/stop transition and fires it at the next
  loop boundary (or immediately, when unquantized).
- `resync_target_us` — "at the next loop" vs "re-align the grid to now".

The Link session has a single owner (the Link service task). Everything
else — web editor, OLED encoder, BLE MIDI transport — posts a
`ControlCommand` onto a queue that the service drains each tick.

### Configuration

One versioned `neon::Config` struct → a single NVS blob (`neon/cfg`) with
magic + version + CRC-32; corrupt or missing blobs fall back to compiled
defaults (`components/neon_core/src/config_model.cpp`, storage behind
`hal::IStorage`). Tempo CV maps the session tempo linearly onto 0–5 V
between configurable min/max BPM, emitted as 12-bit LEDC PWM at ~19.5 kHz
(`halesp::tempo_cv_*`, DAC option open behind the same call shape).

## Inter-core contract (implemented in milestone 2)

Core 0's Link service task captures session state every 10 ms, converts it
to an integer `neon::TimelineSnapshot` (`tempo_mpb_q32`, `origin_us`,
`beat_at_origin_q32`, quantum, playing, peers) via `neon::build_snapshot`,
and publishes it through a single-writer seqlock (`neon::SeqLock`) — but
only when the session *materially* changed (tempo beyond ~0.005 BPM,
transport/peer change, or phase deviating > 1e-4 beats from what the
previous snapshot predicts). Core 1 checks the seqlock version at each
5 ms refill, re-anchors the engine with `ClockEngine::retime()` on a new
version, and projects edges from the snapshot. The engine never calls Link
in the hot path, and core 1 takes no locks — the seqlock payload is stored
as relaxed atomic words fenced seq_cst on both sides, so reads are
retry-based and never block the writer.

`retime()` places tick k of the grid exactly at session beat `k/ppqn`
(anchor rounding < 2^-32 beats, non-accumulating), so clocks from separate
NEON LINK units in the same session land on the same instants.

Later milestones add FreeRTOS queues for config, MIDI, capture, and
transport messages; core 1 still takes no mutexes.

## Ableton Link integration

- `third_party/link` is the official Ableton Link repo, pinned at
  **Link-3.1.5**, vendored as a recursive submodule (brings standalone
  asio). Link Audio needs **Link-4.0**: `CONFIG_NEON_LINK_AUDIO` fails the
  build with a message saying so if the submodule predates it, and
  `ableton::LinkAudio` then replaces `ableton::Link` behind the *same*
  `ILinkSession` — one instance backs both facades. Link is header-only; its ESP32 platform (esp_timer clock, asio
  service task) is auto-selected via ESP-IDF's global `ESP_PLATFORM`
  define.
- `components/ableton_link` wraps it behind `hal::ILinkSession`
  (`ablink::session()`), compiled with `-fexceptions` and
  `LINK_ESP_TASK_CORE_ID=0` so Link's asio service task stays off the
  real-time core. The component also provides the lwIP
  `if_nametoindex`/`if_indextoname` shims the official esp32 example uses.
- `CONFIG_NEON_LINK_STUB=y` swaps in a free-running internal timeline with
  the same interface — CI builds both legs so the project never wedges on
  the Link dependency, and everything downstream of the snapshot is
  provably independent of it.
- WiFi STA credentials come from `CONFIG_NEON_WIFI_SSID/PASSWORD`
  (menuconfig) until the web editor lands in milestone 8; without them the
  module still forms a local Link session.

## Audio engine (docs/AUDIOLINK.md)

Off unless `CONFIG_NEON_AUDIO` is set *and* the stored config enables it.
Everything portable is in `neon_core/audio` and runs under the host tests;
the ESP side is one I2S driver and one service.

- **Pacing.** `IAudioIo::write_block()` blocks on DMA space. That is the
  entire clock of the render loop — no timer, no sleep.
- **Placement.** `neon::SampleClock` keeps an integer affine map between
  the codec's frame count and the esp_timer microsecond domain, fed by
  `(µs, cumulative frames)` marks taken in the I2S completion ISR. The map
  says when the block about to be handed over will actually leave the
  converter; `neon::beat_window` turns that into the session beats it
  covers, in Q32.32. Doubles never enter core 1.
- **Sources.** `ClickSynth` (metronome), `PulseRender` (which drives the
  *same* `MultiClockEngine` the jacks use and maps its edges to sample
  offsets), the synth voice, the Link Audio receive path, and line in.
  Each output channel carries an `AudioRole`: the mix, or a solo tap.
- **Re-anchoring.** Click and pulse scheduling is stateless per block, so a
  new timeline snapshot takes effect within one block (≤2.9 ms). A
  transport stop fades a sounding click over 1 ms rather than cutting it.
- **Link Audio.** `hal::ILinkAudio` (`ablink::link_audio()`) is the seam,
  with beats crossing as Q32.32. The real implementation needs Link 4.0
  (`CONFIG_NEON_LINK_AUDIO`); a no-op stands in otherwise and reports
  `available() == false`, which the editor says out loud. Neither
  direction touches the network from core 1: SPSC rings carry blocks to
  and from a core-0 pump task.
- **Receive.** `JitterBuffer` holds `jitter_ms` of audio and trims a linear
  resampler ±500 ppm from the fill level, absorbing both WiFi jitter and
  the 48 kHz-vs-44.1 kHz rate mismatch.

## Bidirectional operation (milestone 5)

CLK IN / RST IN edges are timestamped by an IRAM GPIO ISR
(`halesp::clkin_capture_*`, esp_timer domain — the same timebase as Link
and the engine) and drained by the Link service, which feeds
`neon::ExtClockEstimator` (pure, host-tested):

- period → outlier rejection (0.5×–2× the running median: bounce and
  dropout guard, with automatic relock when the clock rate genuinely
  halves) → median-of-5 → EMA (α = 1/8) → milli-BPM;
- hysteretic publishing: a new tempo fires only when the estimate leaves a
  0.5% band and settles there — robust following without `setTempo` spam
  that would fight the session;
- 4×-period (min 2 s) silence deactivates the estimator and the module
  reverts to Link-master behavior.

`Config.clock_source` selects kAuto (external wins while CLK IN is
active — default), kLinkMaster, or kExternalMaster; `clock_in_ppqn` sets
the expected input rate. RST IN produces a phase request forwarded to
Link's `requestBeatAtTime`, anchoring the session downbeat to the external
reset.

## Rhythm Explorer + presets + instrumentation (milestone 9)

- **Rhythm modes per clock output** (on top of the rational grid — skipped
  ticks advance silently, so patterns stay session-locked): `all`
  (classic), `euclid` E(fills, steps) with rotation (Bresenham form of
  Bjorklund — canonical up to rotation, hit at step 0, maximal evenness
  property is test-asserted), `pattern` (free assignment via a 64-bit step
  mask), `probability` (per-tick chance). **Chance** (`probability_pct`)
  applies to every mode except `all`, so Euclidean and free-assignment
  patterns can both be thinned; a plain clock is never diced.
  `rhythm_over_loop` distributes the pattern's steps across one loop
  (quantum) instead of the PPQN grid — the "16 steps across 4 beats"
  behavior — by rewriting the channel's rate to `steps/quantum`.
  **Humanize**
  adds a deterministic pseudo-random delay (≤50% of the period); together
  with shuffle the offset is clamped below one period so edges never
  reorder. Probability and humanize decisions hash the absolute tick
  index, so they are reproducible across re-anchors and identical on every
  unit in a session.
- **Presets**: four NVS slots snapshotting the full config (CRC-protected
  like the main blob; WiFi identity excluded on recall). Saved/recalled
  from the web editor (`POST /api/preset?op=save|recall&slot=n`) or via
  **MIDI Program Change** (`n % 4`, disable with `ble.pc_presets`).
- **ISR self-instrumentation**: the pulse ISR records per-edge lateness
  (scheduled vs. actual); max/avg/count appear under `pulse` in
  `GET /api/status`, so the first hardware bring-up quantifies output
  jitter with no extra tooling.

## Web editor + AP setup (milestone 8)

- **REST API** (`components/web_ui`, esp_http_server on all interfaces):

  | Route | Purpose |
  |---|---|
  | `GET /api/status` | BPM, transport, network, peers, ext-clock, loop phase, firmware version, pulse jitter |
  | `GET /api/config` · `PUT /api/config` | The whole config document |
  | `POST /api/transport?op=play\|stop\|toggle\|play_now\|stop_now` | Loop-quantized (or immediate) transport |
  | `POST /api/tempo?bpm=` · `?op=tap\|nudge&delta=\|double\|half` | Tempo control |
  | `POST /api/resync?op=next\|now` | Reset on the next loop, or re-align the grid to now |
  | `GET /api/scan` | Nearby 2.4 GHz networks, for the editor's pick-list |
  | `POST /api/preset?op=save\|recall&slot=n` | Preset slots |
  | `POST /api/ota` | Firmware image upload |
  | `POST /api/factory_reset?confirm=yes` | Erase config + presets, reboot |
  | `POST /api/reboot` | Soft reset |

  PUT semantics: **partial update** — only fields present in the document
  change — then sanitize, live-apply through the milestone-6 pipeline
  (engine seqlock + debounced NVS), and echo the sanitized result.
- **Config JSON** (`neon/config/json.hpp`, portable, host-tested):
  string enums, vendored cJSON (the one C dependency in `neon_core`,
  compiled privately). Passwords are **write-only**: encode emits "" plus
  a `has_pass` flag and decode ignores empty passwords, so secrets never
  round-trip through a browser. Changing a slot's SSID clears that slot's
  stored password rather than pairing it with a different network. The
  64-step pattern mask travels as a hex string — JSON numbers are doubles
  and would lose the top bits.
- **Editor page**: single embedded HTML file (vanilla JS, neon 90s
  styling) served at `/` — a transport bar (play/stop, tap, ±1, ×2/÷2,
  direct BPM, resync, live loop meter), per-output role/shape/rhythm with
  a clickable step grid, engine and session settings, tempo CV, BLE MIDI
  routing, the stored-network list with an in-page scan, access point
  settings, presets, OTA upload, and factory reset; 1 s status ticker.
- **Stored networks**: four slots walked in order, each given
  `wifi_retries` attempts before the station moves on. A successful
  association resets the attempt counter so the working network stays
  preferred.
- **Setup AP**: policy is `fallback` (raise it when the milestone-4
  preference machine's grace expires), `always` (self-host and never join
  a network), or `off`. SSID defaults to `<DEVICE-NAME>-XXXX` from the
  SoftAP MAC and is overridable, as are password, require-password,
  hidden, and channel. A key shorter than the 8 characters WPA2 requires
  leaves the network open rather than failing to start.
- **Identity**: `device_name` is reduced to a DNS-safe label and drives
  both `<name>.local` and the default AP SSID. Renaming re-registers mDNS
  immediately, so the editor URL follows without a reboot.
- **OTA**: two app slots (`ota_0`/`ota_1`). `POST /api/ota` streams the
  image into the inactive slot, validates it, sets the boot partition, and
  reboots; `esp_ota_mark_app_valid_cancel_rollback()` runs once the new
  image's editor is serving, so an image that cannot get that far rolls
  back.

## BLE MIDI + TRS MIDI (milestone 7)

- **Transport** (`components/ble_midi`): NimBLE peripheral advertising the
  MIDI service (03B8…C700 / data I/O 7772…6BF3) as "NEON LINK"; GATT
  writes are copied into a queue and never processed on the NimBLE host
  task. `Config.ble_enabled=0` is the SOFTWARE.md kill switch — the stack
  is fully deinitialized (and a CI leg compiles with `CONFIG_BT_ENABLED`
  unset entirely).
- **Parser** (`neon::BleMidiParser`, portable): BLE-MIDI framing —
  header/timestamp bytes, running status, multi-message packets, realtime
  interleaving, SysEx spanning packets (payload skipped in v1) — hardened
  against pathological input.
- **Routing matrix** (`neon::MidiRouter`, portable, SOFTWARE.md §4):
  notes → mono last-note gate on a configurable target (CLK1–4 or Run;
  point it at a *disabled* clock output so sources don't fight) and/or
  pitch → CV jack at 1 V/oct (5 octaves from C2; overrides tempo CV while
  enabled); CCs → latency and per-clock shuffle (configurable CC numbers,
  CC 123 = all-notes-off); Start/Stop/Continue → Link transport; MIDI
  clock policy ignore/replace/merge for the TRS stream. Program-change
  preset recall arrives with milestone 9.
- **TRS out** (`halesp::midi_uart`, UART1 @ 31.25 kbaud): Link-derived
  24 PPQN clock scheduled by a re-arming esp_timer on the session grid
  (~100 µs accuracy), Start/Stop on transport changes, and BLE
  passthrough per the clock policy.
- **Gates** travel core 0 → core 1 through a queue; the pulse task emits
  them through the same GPTimer emitter with the standard scheduling
  lead, so MIDI gates and clock edges share one hardware path.

## Local UI (milestone 6)

The UI splits portable-vs-driver like everything else:

- `neon::Framebuffer` — 128×64 1bpp in SSD1306 page layout (flush is a
  buffer hand-off), 5×7 font with 2×/3× scaling, ASCII dump for golden
  tests.
- `neon::MenuModel` — encoder-driven state machine over the live
  `neon::Config`: Home → Menu → Outputs → per-clock edit (PPQN, mult,
  div, mode, trigger/duty, shuffle, enable) and Settings (latency, reset
  mode, clock source, CLK IN PPQN, transport gating). Click toggles edit
  mode; rotation adjusts with clamping; `take_dirty()` reports changes
  one-shot.
- `neon::render_ui` — Home shows large BPM, source/transport/network/peer
  status, and a quantum-segmented phase bar; list screens share one
  renderer.
- `components/oled_ui` — ~15 fps core-0 task: esp_lcd SSD1306 over I2C
  (400 kHz; a missing display degrades to LED-only), PCNT quadrature
  encoder (×4 decode, glitch filter, polled click), status LEDs
  (Network = active net, Run = transport, Beat = first 15% of each beat).
- `components/app_state` — the shared buses: the timeline seqlock, an
  `EngineConfig` seqlock (core 1 re-applies + re-anchors on version
  change), peer/ext-clock status atomics, and the config store with
  live-apply + 2 s debounced NVS persistence.

SH1106 (132-column offset variant) is a planned Kconfig option once the
esp_lcd driver line-up covers it; the render path is unaffected.

## Networking (milestone 4)

- **W5500 SPI Ethernet** (`components/net_manager`): SPI2 at 20 MHz with
  IRQ, MAC derived from the SoC's Ethernet MAC, brought up before WiFi.
  The Ethernet netif gets route priority 128 (WiFi STA is 100), so lwIP —
  and therefore Ableton Link, whose scanner enumerates all interfaces —
  prefers the cable whenever it holds an address. Absence of the chip is
  detected at driver install and the module runs WiFi-only.
- **Preference policy** (`neon/net/preference.hpp`, pure logic, host-
  tested): Ethernet-with-IP > WiFi-with-IP > none; a setup-AP
  recommendation fires after a 10 s grace when WiFi was never configured,
  or 60 s when configured networks stay down, and the timer resets on any
  connectivity. The AP itself arrives with the web editor milestone —
  milestone 4 logs the decision.
- **mDNS**: `neon-link.local` (espressif/mdns registry component), for the
  web editor and general discovery.

## Proposed ESP32-S3 pinout

First-pass proposal from the software side (input to HARDWARE.md §13
deliverables); the hardware team owns the final assignment. Constraints:
pulse outputs below GPIO 32 (single-register edge writes), no strapping
pins (0, 3, 45, 46), no flash/PSRAM pins (26–37 on octal-PSRAM modules).

| Function | GPIO | Notes |
|----------|------|-------|
| CLK 1–4 | 4, 5, 6, 7 | 5 V level-shifted outputs |
| RESET | 15 | pulse output |
| RUN | 16 | gate output |
| TEMPO CV | 17 | LEDC PWM → RC filter (0–5 V) |
| MIDI TX | 18 | UART1, 31.25 kbaud, TRS Type A |
| CLK IN | 8 | Schmitt-conditioned input |
| RST IN | 9 | Schmitt-conditioned input |
| W5500 SPI | SCLK 12, MOSI 11, MISO 13, CS 10 | SPI2, short traces |
| W5500 INT / RST | 14 / 21 | |
| OLED I2C | SDA 47, SCL 48 | SSD1306/SH1106 |
| Encoder | A 39, B 40, SW 41 | PCNT quadrature |
| LEDs | NET 42, BEAT 2, RUN 1 | |

## Verification strategy (no-hardware development)

Development happens without an attached ESP32, so:

1. **Host unit tests** (every PR, locally and in CI): all `neon_core`
   logic — edge grids, window continuity, determinism, clamping — plus,
   in later milestones, MIDI parsing, routing, config, estimators, and UI
   state machines. `host/sim/neon_sim` prints edge timelines for manual
   inspection.
2. **CI firmware build** (every PR): `espressif/esp-idf-ci-action` compiles
   the real firmware for `esp32s3` against pinned ESP-IDF v5.3.x.
3. **Bench validation** (deferred): jitter measurement, RF coexistence,
   power draw. Each PR lists its hardware-validation TODOs; milestone 9
   adds on-device ISR-latency self-instrumentation so first bring-up
   quantifies timing without extra tooling.

## Milestone map

Implementation follows SOFTWARE.md §8 exactly, one PR per milestone:
1. Skeleton + CI *(this document's baseline)* → 2. Link peer → 3.
multi-output + latency → 4. Ethernet → 5. external clock in → 6. OLED +
encoder → 7. BLE MIDI → 8. web editor → 9. polish / Rhythm Explorer.

Audio is tracked separately in [`AUDIOLINK.md`](AUDIOLINK.md).

## Licensing

Ableton Link (vendored as a submodule from milestone 2) is dual-licensed
GPLv2+/commercial; firmware distributed with Link falls under GPL
obligations unless a commercial license is obtained (see README).
