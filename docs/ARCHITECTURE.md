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
  else is scheduled at its priority on this core.

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

### Output engine (milestone 3)

`neon::MultiClockEngine` owns six merged, time-ordered channels:

- **CLK 1–4** — independent `PulseChannel`s: per-output PPQN/mult/div,
  trigger or duty-cycle square pulse shape, and shuffle (odd ticks delayed
  by a percentage of the period; parity is derived from the absolute tick
  index so swing feel survives re-anchors). Defaults: 4/2/1/24 PPQN.
- **RESET** — `kStartOfPlay` (one pulse when the transport starts — the
  HARDWARE.md "Reset / Start pulse" default), `kEveryBar` (a 1/quantum
  rational channel while playing), or off.
- **RUN** — gate mirroring the session transport.

Latency compensation is a signed µs offset applied uniformly to every
emitted edge. Clocks free-run on the beat grid by default;
`transport_gating` optionally stops them with the transport (Run/Reset
follow transport either way).

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
  asio). Link is header-only; its ESP32 platform (esp_timer clock, asio
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

## Licensing

Ableton Link (vendored as a submodule from milestone 2) is dual-licensed
GPLv2+/commercial; firmware distributed with Link falls under GPL
obligations unless a commercial license is obtained (see README).
