# Research Spike — A Phase-Locked Loop for MIDI Clock Sync-In

**Status:** research spike — this document is the deliverable; no firmware
changes proposed for merge yet.
**Question:** how should the module lock its timeline to an *incoming* MIDI
clock stream (0xF8 at 24 PPQN over TRS MIDI IN, BLE MIDI, or the USB-MIDI
adapters), and is a phase-locked loop the right shape for it?
**Answer (short):** yes — a two-loop (phase + rate) digital PLL, and the
codebase already contains 80 % of the design. `SampleClock` *is* that PLL for
the audio domain; `ExtClockEstimator` supplies the acquisition/outlier
machinery; the timeline seqlock is the natural output seam. What is genuinely
missing is smaller and more mundane than the control theory: **no MIDI input
path currently carries a timestamp**, and a PLL cannot lock to events that
have no arrival time. Fix the plumbing, port the `SampleClock` servo to the
beat domain, and the rest is tuning.
**Date:** 2026-08-27

---

## 1. Why the existing external-clock path is not this feature

The module already follows an external clock — but only the *analog* one.
CLK/RST IN edges are timestamped in an IRAM GPIO ISR
(`components/neon_hal_esp/src/clkin_capture.cpp`), drained by the Link
service (`main/link_service.cpp:233-270`), and fed to
`neon::ExtClockEstimator`
(`components/neon_core/include/neon/ext_clock.hpp`), which drives
`session.set_tempo()` and, on a RST IN edge, `request_beat_at_time()`.

`ExtClockEstimator` is deliberately **not a PLL**. It is a frequency
estimator with hysteretic, stepped publishing:

- period → 0.5×–2× median outlier gate → median-of-5 → EMA (α = 1/8) →
  milli-BPM (`ext_clock.cpp:49-143`);
- a new tempo is published only when the estimate leaves a **0.5 % band**
  around the last published value and settles there for 3 pulses
  (`kHysteresisDen = 200`, `kSettlePulses = 3`);
- phase is corrected **only** by an explicit RST IN pulse
  (`on_reset` → `take_phase_request`).

That design is correct for its input. Modular clock has a dedicated reset
line, so phase comes for free, and hysteresis keeps `setTempo` spam from
fighting the Link session. But aimed at MIDI clock it fails on three counts:

1. **MIDI clock has no reset line.** Phase must come from the tick stream
   itself: tick *n* after Start *is* beat *n*/24, by definition. An
   estimator that only measures periods throws that information away.
2. **The hysteresis band is a standing phase-error integrator.** 0.5 % at
   120 BPM is 0.6 BPM of tolerated tempo error — the follower walks off the
   sender's grid by a full beat every ~170 beats and nothing corrects it,
   because on this input nothing ever calls `on_reset`.
3. **Stepped tempo publishes are audible on the outputs.** Each publish is a
   discontinuity the pulse engine re-anchors around. Fine at the rate a
   human turns an encoder; not fine when a DAW sends a smooth tempo ramp
   that the band quantizes into stair-steps.

Meanwhile the actual MIDI-clock input paths do exist — and go nowhere near
the timeline. `MidiRouter::on_realtime()` forwards 0xF8/FA/FB/FC to the TRS
output (`components/neon_core/src/midi_router.cpp:92-119`, gated by
`ClockPolicy`), and Start/Stop can poke the transport, but incoming clock
*ticks* never steer tempo or phase on any target. `HANDOFF.md:32` confirms
CLK IN is today the only external sync on every port.

## 2. The blocking defect: no timestamps anywhere in the MIDI RX path

A PLL's measurement is "tick *n* arrived at time *t*". Today no layer can
say that:

- `IMidiSink::on_realtime(uint8_t status)`
  (`components/neon_core/include/neon/midi/ble_midi_parser.hpp:21`) carries
  **no time argument** at all.
- `BleMidiParser` **skips** the BLE-MIDI timestamp bytes without decoding
  them (`ble_midi_parser.cpp` — timestamps are recognized only to be
  discarded). The BLE-MIDI spec puts a 13-bit millisecond timestamp on
  every event *at the sender*; this is exactly the de-jittering information
  a BLE clock consumer needs, and we drop it on the floor.
- `SerialMidiParser::feed(uint8_t)` is fed from the UART driver's buffered
  reads; by the time bytes reach the parser their arrival time has been
  quantized by the RX FIFO threshold/timeout, not captured per byte.
- The USB adapters (`adapters/*/src/main.cpp`) forward bytes over a UART
  bridge with the same property.

So step zero of any MIDI-sync feature — PLL or otherwise — is timestamp
plumbing (§6.1). This is the only part of the work that touches interfaces.

## 3. The plant: what a MIDI clock stream actually looks like

24 ticks per quarter note. At 120 BPM the tick period is 20.833 ms
(tick rate 48 Hz); the musical mapping is exact: **tick *n* ⇔ beat *n*/24**,
with Start (0xFA) defining tick 0 = beat 0, Continue (0xFB) resuming from
the last Song Position (0xF2, one SPP unit = one sixteenth = 6 ticks), and
Stop (0xFC) freezing the count. Many devices keep sending 0xF8 while
stopped (so followers can pre-lock tempo); the design must track tempo
while stopped without moving the transport.

Jitter and delay differ enormously by transport, which drives every
bandwidth decision below:

| Transport | Quantization / jitter floor | Typical observed jitter | Notes |
|---|---|---|---|
| TRS/DIN serial, 31.25 kbaud | 320 µs per byte | 0.1–1 ms RMS | A clock byte queues behind an in-flight 3-byte message (~1 ms). Sender-side scheduling (hardware sequencers) is usually the clean case. |
| USB-MIDI via our adapters | 1 ms USB frame | 1–3 ms | Full-speed USB batches into 1 ms frames; our adapter then re-serializes onto UART. DAW senders add scheduler jitter on top. |
| BLE MIDI | connection interval **7.5–15 ms** (can be 30+ ms) | bursty: several ticks arrive in one packet | *Arrival* times are nearly useless; the in-packet 13-bit ms timestamps restore sender-side spacing to ±1 ms. Two-clock problem: sender timestamp domain drifts vs. `esp_timer` and wraps every 8.192 s. |

Plus slow terms: the sender's crystal is tens of ppm off ours (the same
±300 ppm reality `PeerClock` caps its drift fit at,
`components/neon_sync/src/peer_clock.cpp:127-135`), and tempo itself may
ramp (automation) or step (a human typing 128).

Design consequence: **one loop, per-transport gains.** The DIN path can run
a much tighter loop than BLE; the loop structure need not differ.

## 4. Prior art already in the tree (this is why the spike is short)

`neon::SampleClock`
(`components/neon_core/include/neon/audio/sample_clock.hpp`) is a working,
host-tested, integer-only two-loop PLL disciplining the affine map
*frames → µs* from noisy DMA marks:

- **Phase loop:** re-anchor on every mark, pulled `residual/16` toward the
  observation (`kPhaseShift = 4`) — a first-order low-pass on phase.
- **Rate loop:** a two-point measurement over a ≥ 0.2 s baseline between
  *filtered* anchors, folded in as a bounded ppm step
  (`kRateDivisor`, `kMaxPpmStep = 20 ppm/mark`, clamp `±kMaxPpm`) — with
  the header's own warning that per-mark rate estimates are quantization
  noise and integrating them makes a servo chase its rounding.
- Outlier → re-anchor and drop the rate baseline (`kOutlierUs`);
  periodic rebase so slow thermal drift stays tracked; `locked()` after
  `kLockMarks`.

Substitute *marks* → *MIDI ticks*, *frames* → *ticks/24 beats*, and
*µs-per-frame* → *µs-per-beat* (already the timeline's native
`tempo_mpb_q32` Q32.32 unit, `components/neon_core/include/neon/timeline.hpp:16`)
and this is precisely the MIDI PLL. The remaining in-tree donors:

- `ExtClockEstimator`: the median window, the 0.5×/2× bounce/dropout gate,
  the consecutive-long-reject relock, and the activity timeout
  (`ext_clock.cpp:59-80,150-164`) — acquisition and self-healing.
- `nsync` slew discipline: neon-sync bounds every phase correction to a
  **slew rate (default 1000 µs/s)** so the grid glides under the pulse
  engine instead of stepping (`docs/NEON_SYNC.md` §4.2). A MIDI follower
  must do the same — its consumers are the same four hardware outputs.
- `host/tests/test_ext_clock.cpp`: the synthetic-stream test idiom (steady,
  jittered, bounce, dropout, rate-change streams) to replicate for the PLL.

## 5. Proposed design: `neon::MidiClockPll` (pure, host-tested)

A portable class in `neon_core`, integer math throughout, no OS calls —
same discipline as every other estimator in the tree.

### 5.1 State and interface

```
class MidiClockPll {
  // Inputs (timestamps mandatory, esp_timer/shared µs domain):
  void on_tick(int64_t t_us);        // 0xF8
  void on_start(int64_t t_us);       // 0xFA: next tick is tick 0 (beat 0)
  void on_continue(int64_t t_us);    // 0xFB: next tick resumes tick count
  void on_stop(int64_t t_us);        // 0xFC: freeze transport, keep tempo lock
  void on_spp(uint16_t sixteenths);  // 0xF2: tick count := sixteenths * 6
  void set_transport_gains(Transport t);  // kDin / kUsb / kBle gain set

  // Outputs:
  bool active(int64_t now_us);       // ticks flowing (ExtClock-style timeout)
  bool locked() const;               // residual EMA under threshold, N ticks
  // The model itself: beat(t) as (beat_at_origin_q32, origin_us,
  // us_per_beat_q32) — one-shot "materially changed" flag for Link builds,
  // continuous read for internal-timeline builds.
};
```

State: tick counter (int64, so pre-Start free-running clock counts
relative), model `(origin_us, beat_at_origin_q32, us_per_beat_q32)`, the
`SampleClock`-shaped servo state (anchor, rate baseline, residual EMA), and
a 3-state machine **unlocked → acquiring → locked**.

### 5.2 Per-tick algorithm

1. **Gate** the inter-tick period exactly as `ExtClockEstimator` does
   (0.5×–2× running median; drop bounces, resync-don't-record dropouts,
   relock after 3 consecutive longs). MIDI over BLE *will* deliver
   back-to-back ticks from one packet — the raw-arrival gate handles that
   even before BLE timestamps are decoded.
2. **Acquire** (first ~4 accepted ticks): seed `us_per_beat` directly from
   the median period × 24; anchor phase on the latest tick. This is
   deliberately the current estimator's behavior — fast capture, no loop
   dynamics yet.
3. **Track** (the PLL proper):
   - Predicted time of tick *n*: `t_pred = model(beat = n/24)`.
   - Phase error `e = t_obs − t_pred`, clamped Huber-style at ±¼ tick
     period so one ugly tick cannot yank the anchor.
   - **Phase loop:** re-anchor at `t_pred + e/2^kP` (α = 1/16 on DIN;
     1/32 on USB; 1/64 on BLE raw arrivals).
   - **Rate loop:** two-point µs-per-beat over a ≥ 1-beat, ≥ 250 ms
     baseline between filtered anchors; fold in as a slew-limited ppm step
     (±20 ppm/tick tracking limit), clamp ±10 %.
   - **Gear shift:** if `|e|` exceeds ~½ tick period for ≥ 6 consecutive
     ticks, the sender stepped its tempo — drop back to *acquiring*
     (reseed rate from the recent median, keep the tick count). This gives
     fast relock on a 120→128 jump without loosening the locked-state
     bandwidth that makes steady jitter invisible.
4. **Transport events** are phase *facts*, not filtered inputs: Start/SPP
   set the beat number of the next tick exactly; Stop freezes beat advance
   but the rate loop keeps running on incoming ticks (the free-clock case).

### 5.3 Loop dynamics, in numbers

At 120 BPM the tick rate is 48 Hz. α = 1/16 phase gain gives a first-order
phase-loop corner of ≈ 0.5 Hz (time constant ~16 ticks ≈ 330 ms); the rate
loop's long baseline makes the pair behave like a heavily over-damped
type-II loop — no overshoot, zero steady-state phase error at constant
tempo, bounded lag under a ramp. Expected performance (to be verified in
the prototype, §7):

- 1 ms RMS input jitter (DIN worst case) → output grid wander of roughly
  1 ms/√16 ≈ **±250 µs**, well under one 48 kHz audio buffer and far under
  anything musically audible;
- BLE with decoded 13-bit timestamps behaves like the DIN case shifted by
  the (filtered) sender-clock offset; without them, α = 1/64 still holds
  the grid but relock on tempo steps stretches to a few seconds — decoding
  the timestamps is worth it (§6.1).
- Lock declaration: residual EMA < 1 ms for 24 ticks (one beat) on DIN.

### 5.4 What the PLL feeds, per target

The output seam already exists on every port; the PLL is "just another
timeline writer":

- **ESP32 / Link builds:** steer the session the way `ext_clock` does
  today from `main/link_service.cpp` — `set_tempo()` rate-limited (reuse
  the correction-gap idea from `nsync::DawFollower::can_correct`,
  `components/neon_sync/src/daw_follower.cpp:40-48`) and
  `request_beat_at_time()` on lock and on Start/SPP, then let Link
  propagate. The hysteretic *publish* stays at this layer (Link peers
  shouldn't see 0.1-BPM chatter), but it wraps a *continuous* internal
  model — the local outputs follow the PLL, not the published steps.
- **Teensy/Daisy internal-timeline builds:** publish the model directly as
  the `TimelineSnapshot` — the PLL *is* the session. Precedence follows
  `docs/DAISY.md` §clocking: CLK IN outranks MIDI clock when both are
  alive; Link (where it exists) is outranked by both under `kAuto`.
- `Config.clock_source` grows `kMidiMaster`, and `kAuto`'s priority order
  becomes CLK IN > MIDI IN > session, with `app_status_ext_clock`-style
  surfacing to the OLED/web status strip.

## 6. The integration work, honestly sized

### 6.1 Timestamp plumbing (the real work, ~2–3 days)

1. `IMidiSink::on_realtime(uint8_t status)` →
   `on_realtime(uint8_t status, int64_t t_us)`; mechanical sweep of the
   router, parsers, services, adapters, and host tests.
2. **UART RX:** timestamp at the driver event with the FIFO
   compensation subtracted (bytes × 320 µs), or — cleaner and matching the
   house style — an RX-pin edge note in the existing pattern of
   `clkin_capture`. First version: event-time minus FIFO depth is within
   ~1 byte time and lets the PLL's own filter absorb the rest; measure
   before building the fancier path.
3. **BLE:** decode the 13-bit millisecond timestamps in `BleMidiParser`
   (currently skipped) and maintain a sender-time → `esp_timer` offset with
   a small median filter, unwrapping the 8.192 s modulus against packet
   arrival time. This is a miniature `PeerClock` and can borrow its shape.
4. Adapters forward raw bytes; their clock ticks are timestamped at the
   module's UART like any DIN input — no adapter firmware change needed.

### 6.2 The PLL itself (~2 days to first green tests)

`components/neon_core/{include/neon/midi/clock_pll.hpp,src/midi_clock_pll.cpp}`
plus `host/tests/test_midi_clock_pll.cpp` cloned from the `test_ext_clock`
idiom, with the jitter models of §3 as synthetic streams.

### 6.3 Service wiring (~1–2 days per target family)

ESP32 `link_service.cpp` first (it is where `ext_clock` already lives and
where precedence is arbitrated); Teensy/Daisy afterward, where the change
is smaller (publish the snapshot).

## 7. Alternatives considered

| Option | Verdict |
|---|---|
| **Feed 0xF8 into `ExtClockEstimator` at `ppqn = 24`** (zero new code) | Rejected as the end state — no phase tracking (§1), hysteresis stalls, stepped output. **Accepted as the bring-up stopgap**: once timestamps exist (§6.1) this is a one-line wiring change and gives tempo-follow while the PLL lands. |
| **2-state Kalman filter (phase, tempo)** | The statistically optimal version of the same loop; gains become time-varying and the innovation gate falls out for free. Costs float math and tuning opacity on core-1-adjacent code. The PI/two-loop form is equivalent at fixed gains, matches `SampleClock`, and stays integer. Revisit only if the prototype can't tune fixed gains across 60–200 BPM. |
| **FLL + phase snap on Start only** (what some hardware does) | Simplest, but drifts within a long song exactly like §1's failure mode; musicians running 10-minute sets off a DAW will hear the flams by the end. |
| **Per-tick proportional correction, no rate memory** (uClock-style smoothing) | Tracks, but either chases jitter (high gain) or lags tempo ramps by a constant phase error (low gain); a type-II loop does both jobs at once. |

## 8. Risks and open questions

1. **Chase vs. flywheel feel.** How fast the follower answers a nudge is a
   *musical* preference (hardware boxes answer instantly and jitter;
   DAW-style smoothing feels rubbery). The gear-shift (§5.2.3) is the
   proposed compromise; expose the tracking bandwidth as one config enum
   (`tight / smooth`) rather than raw gains.
2. **Double/half-tempo lock.** A dropout-heavy stream can median-lock at
   half rate. The consecutive-long-reject relock covers the analog case;
   the MIDI case is easier (tick count is authoritative) but the test
   suite must include it.
3. **Sender misbehavior catalogue:** clock-while-stopped (handled, §5.2.4),
   Continue without SPP, SPP mid-play, ticks before any Start (free-run:
   tempo lock, phase provisional until a transport fact arrives). Each is a
   host test, not a design change.
4. **Link-side authority conflicts.** When the module follows MIDI *and*
   has Link peers, our `set_tempo` writes fight any peer edits — the same
   authority question `DawFollower` answers with rate-limited corrections
   and `require_peer`; reuse that policy verbatim.
5. **BLE timestamp trust.** Some senders stamp poorly or not at all
   (timestamp = arrival bucket). The offset filter must detect a
   degenerate timestamp stream (variance ≈ connection-interval comb) and
   fall back to raw-arrival gains.

## 9. Recommended next step: a 2–3 day bench spike

1. Land the host-side `MidiClockPll` + synthetic-stream tests (no firmware
   risk, pure code).
2. Timestamp plumbing for **DIN UART only**; wire `clock_source = kAuto`
   precedence on the ESP32 target behind a config flag.
3. Bench acceptance, using the `BENCH_NO_SCOPE.md` telemetry rig:
   - lock from cold in < 2 bars at 120 BPM (DIN);
   - steady-state grid error vs. sender < ±500 µs RMS (DIN), no drift over
     10 minutes;
   - 120→128 BPM step: relocked in < 4 s, output tick period monotonic
     during the transition (no reversal, no double tick);
   - kill the sender: `active()` false within 4 tick periods, clean
     reversion to session tempo (the existing `ext_clock` timeout
     semantics).
4. Only then decide BLE timestamp decoding scope (it is separable and the
   riskiest plumbing).
