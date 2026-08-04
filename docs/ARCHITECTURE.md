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
a quotient/remainder accumulator: the per-tick period is
`mpb / ppqn` kept as `(q, r)`, and the remainder accumulates so that every
`ppqn` ticks sum to *exactly* `mpb` — the grid cannot drift from the ideal
beat grid no matter how long the set runs. Host tests assert this
bit-exactly (`host/tests/test_clock_engine.cpp`).

The ESP32-S3 FPU is single-precision only; doubles are software-emulated
and are banned from anything reachable by the pulse path.

## Planned inter-core contract (milestone 2)

Core 0 publishes a `TimelineSnapshot { generation, tempo_q32,
beat_origin_us, quantum, phase, running, latency_us }` through a seqlock;
core 1 reads it lock-free at each refill and projects upcoming edges from
the snapshot. The engine never calls Link in the hot path. Config, MIDI,
capture, and transport messages travel over FreeRTOS queues; core 1 takes
no mutexes.

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
