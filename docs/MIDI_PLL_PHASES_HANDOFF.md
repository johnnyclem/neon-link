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
| BLE 13-bit timestamp decode + sender-time mapper (Phase B) | `ble_midi_parser.*`, `ble_time_mapper.hpp`/`src/midi_ble_time_mapper.cpp`, follower integration | Done, host-tested (`test_ble_time_mapper.cpp`) |
| ESP32 wiring (queue, arbitration, session drive) | `app_state/timeline_bus.*`, `main/midi_service.cpp`, `main/link_service.cpp` | Done, **not yet compiled by CI** (see followups §1) |
| Daisy parser timestamps | `daisy/src/midi_daisy.cpp` (`drain_rx` passes `now_us`) | Done — the router sync tap feeds the link service's `SyncFollower` (Phase C) |

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
### ✅ Done (2026-08-28)

Landed as designed; BLE with a conforming sender is now a first-class
source (honest `locked()`, kUsb-grade gains) instead of the documented
degraded mode. What shipped, mapped onto the original items:

1. **Parser:** `BleMidiParser` reconstructs the 13-bit millisecond value
   (header bits 12–7, timestamp-byte bits 6–0, low-byte rollover within
   a packet) and delivers it via the widened
   `IMidiSink::on_realtime(status, t_us, sender_ms13)`
   (`kNoSenderMs` from stamp-less feeders — the serial parser, or a
   malformed packet). Fixture-packet host tests in `test_midi.cpp`.
2. **Domain mapping:** `neon::midi::BleTimeMapper`
   (`ble_time_mapper.hpp` / `src/midi_ble_time_mapper.cpp`) — the
   miniature PeerClock: offset = arrival_us − sender_ms·1000 through a
   16-sample median window with a 2 s TTL, the modulus unwrapped
   against packet arrival (which also bounds it: a > 2 s gap resets).
   Host tests: `test_ble_time_mapper.cpp`.
3. **Degenerate-sender detection:** dispersion of the *decoded*
   inter-tick spacing (mean absolute deviation vs. its median) reads
   the connection-interval comb; a comb-shaped window revokes trust
   immediately and the stream stays on raw arrivals with
   `Transport::kBle` gains. Trust needs a full window plus eight clean
   evaluations (~one beat) — decoded spacing is immune to link jitter,
   so a dirty window indicts the sender, not the radio.
4. **Gains:** integrated in `SyncFollower::on_event` — BLE ticks run
   through the mapper, and while it is `trusted()` the PLL runs the
   `kUsb` gain set on mapped times (see the follower tests in
   `test_midi_sync_follower.cpp`: stamped BLE locks, degenerate BLE
   keeps the old degraded behavior). Still worth doing when hardware
   allows: capture a real-device packet trace and replay it as a
   parser fixture — the synthetic streams model the comb, not any one
   stack's quirks. Note the mapped grid sits at the *median* delivery
   latency (a constant the loop never sees); absolute-latency
   compensation, if ever wanted, is a nudge-config question, not an
   estimator one.

## Phase C — Internal-timeline targets: Teensy 4.1 and Daisy  *(done)*

On boards where the timeline is not a Link session, the PLL **is** the
session while active (`HANDOFF.md` root doc: ext-clock is more central on
these targets). The PLL's `Model` maps 1:1 onto `TimelineSnapshot`
(`tempo_mpb_q32` / `origin_us` / `beat_at_origin_q32` / `playing`), so
this phase was service glue, not estimator work. How it landed:

- **Daisy** (`daisy/src/link_service_daisy.cpp` + `midi_daisy.cpp`): the
  `Sink` grew `midi_clock_byte` / `midi_song_position` overrides that
  call `on_event` directly on the link service's `SyncFollower`
  (single-threaded main loop — no queue; registered via
  `miditrs::set_sync_follower`). This replaced the raw-tick
  `ExtClockEstimator` stopgap (spike §7) and its clock ring. The
  arbitration mirrors the ESP service: CLK IN outranks MIDI under
  `kAuto`, each master mode pins its own source, and the follower keeps
  the PLL warm while not allowed. On capture, while following with no
  Link peers, the PLL model overrides the captured session state before
  `build_snapshot` — the outputs ride the continuous estimate (and the
  existing material-change gate keeps publish churn down), while the
  hysteretic `set_tempo` steering keeps the session's own tempo in step
  for clean reversion when the sender dies. On the netlink build with
  peers, the steered session capture stands (session consensus owns the
  grid, like the ESP path).
- **Teensy** (`teensy41/src/midi_t41.h`): documented "not on this
  target" — Serial1 RX (pin 0) is reserved but not wired, so there is no
  MIDI-in path to feed. When MIDI in lands (TEENSY41.md §5.3) the sync
  side copies the Daisy shape.
- **Position, not just tempo:** while `beat_valid`, `origin_us` /
  `beat_at_origin_q32` / `playing` come straight from the model — SPP
  and bar position land exactly. While `false` (free clock), only the
  tempo is overridden; the captured session phase keeps the local beat
  continuous.
- The MIDI-out seam needed no new config: the existing clock policy
  already expresses thru vs regeneration. `kIgnore` (default) emits the
  snapshot-derived clock, which while following *is* the PLL grid —
  regeneration; `kReplace` mutes that stream and forwards the sender's
  own bytes — thru; `kMerge` sends both.

Still owed to close the phase completely: a bench pass of the Daisy path
(the Phase A rig applies unchanged — TRS sender into the Seed/Pod MIDI
IN), since the service glue is on-device code the host tests don't cover.

## Phase D — Authority interplay with Link peers (spike §8.4) ✅ done

When the module follows MIDI *and* has Link peers, its `set_tempo` writes
compete with peer edits. `nsync::DawFollower`'s policy
(`components/neon_sync/src/daw_follower.cpp`) is now adopted in
`SyncFollower` (host-tested, `test_midi_sync_follower.cpp`):

- `poll()` takes a `SessionView` (the owner's last capture; the ESP32
  loop passes its previous-iteration capture). Omitting it degrades to
  the old pure-edge behavior, so targets without one keep working.
- **Level-assert while the MIDI transport runs:** tempo compares against
  the *session's* value, so a peer edit reads as divergence and is
  re-asserted under the existing 0.5 %/1 s hysteresis; a peer stop under
  a running sender is corrected back to playing, rate-limited.
- **Edges-only while stopped:** the comparison reference stays our own
  last publish, so peer edits stand between MIDI tempo moves, and peers'
  transports are never touched.
- **`require_peer`** (`set_require_peer`, default off — here the session
  is also the local timeline, so following must work alone): when on,
  all writes are held at zero peers, with a warm, downbeat-preserving
  handover when one joins. Not yet exposed in config; wire it to a
  stored field if a bridging use case ever wants it persisted.

What remains bench-side: confirm on real hardware (Phase A rig, with a
peer riding the session tempo) that the correction cadence feels right;
the knobs are the same `kHysteresisDen`/`kTempoGapUs` pair.

## Phase E — Optional/opportunistic

- **USB-MIDI native path:** the adapters (`adapters/*`) bridge USB to the
  UART, so they already ride the DIN path with DIN gains; the `kUsb` gain
  set only becomes reachable if a native USB host/device MIDI-in ever
  lands. No work now; the enum is ready.
- **Telemetry:** a `midi_pll` CSV emitter (residual, tempo, lock state per
  tick) through `components/neon_core/src/telemetry/` would make Phase A
  measurable without a scope and future regressions visible. Cheap and
  worth doing during Phase A rather than after.
