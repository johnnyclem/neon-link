# Handoff — MIDI Clock Sync-In: Remaining Phases

**Scope:** the remaining arc of `docs/SPIKE_MIDI_PLL.md` after PR #40 —
the phases that need hardware, new estimator work, or another target's
service layer. Small finishing chores live in
`docs/MIDI_PLL_FOLLOWUPS_HANDOFF.md`. Phases here are ordered by
recommended sequence, but B and C are independent of A and of each other.

**What already exists (don't rebuild it):**

| Piece | Where | State |
|---|---|---|
| The PLL (type-II PI servo, LS-slope seeding, transport semantics) | `components/neon_core/{include/neon/midi/clock_pll.hpp, src/midi_clock_pll.cpp}` | Done, host-tested (`host/tests/test_midi_clock_pll.cpp`) |
| Follower policy (hysteresis, rate limit, handover) | `neon/midi/sync_follower.hpp`, `src/midi_sync_follower.cpp` | Done, host-tested (`test_midi_sync_follower.cpp`) |
| Timestamped parse path + router sync tap | `ble_midi_parser.*`, `serial_midi_parser.*`, `router.hpp`/`midi_router.cpp` | Done, host-tested |
| ESP32 wiring (queue, arbitration, session drive) | `app_state/timeline_bus.*`, `main/midi_service.cpp`, `main/link_service.cpp` | Done, **not yet compiled by CI** (see followups §1) |
| Daisy parser timestamps | `daisy/src/midi_daisy.cpp` (`drain_rx` passes `now_us`) | Done — but nothing consumes the tap yet (Phase C) |

---

## Phase A — Bench acceptance (spike §9.3)  *(needs hardware; first)*

The loop constants were chosen analytically and validated against
synthetic streams; the bench run is what earns them trust. Use the
`docs/BENCH_NO_SCOPE.md` telemetry rig. Sender: any hardware sequencer
over TRS first (the clean case), then a DAW over a USB interface into the
TRS input.

Acceptance, per the spike:

1. **Lock from cold < 2 bars at 120 BPM** (DIN). The host tests lock in
   ~33 ticks; the bench number includes real UART timestamp noise.
2. **Steady-state grid error < ±500 µs RMS** vs the sender, no drift over
   10 minutes. Measure via a CLK output against the sender's own clock
   out, or the telemetry CSV path (`components/neon_core/src/telemetry/`).
3. **Tempo step 120→128: relocked < 4 s**, output tick period monotonic
   through the transition (no reversal, no double tick at the outputs).
4. **Kill the sender:** follower inactive within its timeout
   (max(500 ms, 8 ticks)), clean reversion to the session tempo, and —
   important, new vs the spike's wording — no transport glitch: the
   session keeps playing (the follower only sends edges).
5. **Clock-while-stopped:** DAW sending free clock, then Start —
   downbeat lands on the Start tick, tempo already locked.

**Tuning knobs, should a criterion fail** (all in
`clock_pll.hpp`/`.cpp`, `Gains` + constants; every one is host-testable
before reflashing):

| Symptom | Knob |
|---|---|
| Grid wanders with jitter | raise `phase_shift` (smaller α) |
| Tempo readout breathes | raise `rate_shift` (smaller β) — keep β ≈ α²/4 |
| Ramp lag trips the step detector | lower `rate_shift` or raise the T/2 step threshold |
| Slow relock on steps | lower `step_ticks`, or widen the reseed window logic |
| Premature lock claims | raise `kLockTicks` or tighten the T/8 lock band |

If the 0.5 %/1 s publish policy makes Link peers feel rubbery on tempo
rides, that's `SyncFollower::kHysteresisDen`/`kTempoGapUs` — a *musical*
call (spike §8.1); consider exposing a `tight`/`smooth` config enum
mapping to two gain sets rather than raw numbers.

## Phase B — BLE-MIDI 13-bit timestamp decoding (spike §6.1.3, §8.5)

Raw BLE arrivals are the documented degraded mode: the PLL deliberately
never reports `locked()` under burst delivery, and tempo is only good to a
few percent. Decoding the in-packet timestamps recovers sender-side
spacing (±1 ms) and makes BLE a first-class source.

Design already sketched in the spike; the shape:

1. **Parser:** `BleMidiParser` currently recognizes timestamp bytes only
   to skip them. Extend it to reconstruct the 13-bit millisecond value
   (header carries bits 12–7, each message's timestamp byte bits 6–0;
   handle the low-byte rollover *within* a packet) and deliver it — either
   widen `on_realtime` again with a `sender_ms` or add a parallel
   callback. Parser stays pure; host tests with fixture packets.
2. **Domain mapping (the real work):** sender ms → local µs is a miniature
   `PeerClock` (`components/neon_sync/src/peer_clock.cpp` is the in-tree
   prior art): offset = arrival_us − sender_ms·1000, filtered by a small
   median window, **unwrapped against the 8.192 s modulus** using packet
   arrival as the coarse reference. Sender clock drifts tens of ppm —
   the median window's TTL handles it like PeerClock's does.
3. **Degenerate-sender detection (spike §8.5):** some stacks stamp at the
   connection-interval bucket, making timestamps worthless. Detect: if the
   decoded inter-tick spacing shows the same comb as raw arrivals
   (variance ≈ connection interval), fall back to raw-arrival mode
   (`Transport::kBle` gains) instead of trusting the stamps.
4. Once mapped times feed the PLL, BLE can likely run the kUsb gain set;
   revisit `Gains` and add a host test with a recorded real-device packet
   trace if one can be captured (the parser fixtures make this cheap).

## Phase C — Internal-timeline targets: Teensy 4.1 and Daisy

On boards where the timeline is not a Link session, the PLL **is** the
session while active (`HANDOFF.md` root doc: ext-clock is more central on
these targets). The PLL's `Model` maps 1:1 onto `TimelineSnapshot`
(`tempo_mpb_q32` / `origin_us` / `beat_at_origin_q32` / `playing`), so
this phase is service glue, not estimator work:

- **Daisy** (`daisy/src/link_service_daisy.cpp` + `midi_daisy.cpp`): the
  parser already delivers timestamps. Add `midi_clock_byte` /
  `midi_song_position` overrides to its `Sink`, feed a `SyncFollower`
  (single-threaded main loop — no queue needed, call `on_event` directly),
  and on `poll()`: while following, publish the PLL model as the timeline
  snapshot instead of the internal one. Precedence per `docs/DAISY.md`:
  CLK IN outranks MIDI when both are alive.
- **Teensy** (`teensy41/src/link_service_t41.cpp`): same shape; it has
  TRS MIDI out today but check whether MIDI *in* is wired before starting —
  if there's no RX path, this reduces to documenting "not on this target".
- **Position, not just tempo:** unlike the Link path (which only anchors
  the downbeat), internal-timeline targets can consume
  `beat_at_origin_q32` directly — SPP and bar position land exactly. Use
  `beat_valid` to decide whether to trust it; while `false` (free clock),
  follow tempo only and keep the local beat continuous.
- Watch the seam: these targets re-derive `next_clock_tick_us` for MIDI
  *out* from the snapshot — when the snapshot follows MIDI *in*, the
  module would echo the sender's clock back out. Decide (config) whether
  clock-thru or clock-regeneration is wanted; regeneration is what the
  PLL gives for free, thru is what `ClockPolicy::kReplace` already does.

## Phase D — Authority interplay with Link peers (spike §8.4)

When the module follows MIDI *and* has Link peers, its `set_tempo` writes
compete with peer edits. The follower's rate limiting is the first
defense; if fighting is observed on the bench (tempo ping-pong in the
session), adopt `nsync::DawFollower`'s policy verbatim
(`components/neon_sync/src/daw_follower.cpp`): level-assert while the
MIDI transport *runs* (the sender is authoritative), edges-only while
stopped, optional `require_peer`. This is deliberately deferred until the
bench shows it's needed — don't build it speculatively.

## Phase E — Optional/opportunistic

- **USB-MIDI native path:** the adapters (`adapters/*`) bridge USB to the
  UART, so they already ride the DIN path with DIN gains; the `kUsb` gain
  set only becomes reachable if a native USB host/device MIDI-in ever
  lands. No work now; the enum is ready.
- **Telemetry:** a `midi_pll` CSV emitter (residual, tempo, lock state per
  tick) through `components/neon_core/src/telemetry/` would make Phase A
  measurable without a scope and future regressions visible. Cheap and
  worth doing during Phase A rather than after.
