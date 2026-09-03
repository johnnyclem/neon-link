#pragma once

// Phase-locked loop that disciplines the local timeline to an incoming
// MIDI clock stream (docs/SPIKE_MIDI_PLL.md). Pure logic — the service
// layer feeds realtime bytes with microsecond timestamps; host tests
// drive synthetic streams.
//
// The plant: 0xF8 at 24 PPQN. Unlike the analog CLK IN path, every tick
// is authoritative — tick n after Start *is* beat n/24 by definition, so
// the tick counter never drops an edge and phase comes from the stream
// itself, not from a reset line. What varies is only the timestamp
// quality (DIN ~0.3-1 ms, USB ~1-3 ms, BLE bundles several ticks onto
// one arrival time), which the loop filters.
//
// Two coupled loops, per tick (a type-II PI servo — the same shape as
// SampleClock's phase/rate pair, but with the integral folded in per
// tick so smooth tempo ramps track with constant lag instead of
// stair-stepping):
//
//   e       = t_observed - t_predicted(tick n), Huber-clamped at ±T/4
//   anchor += e / 2^kP           (phase: first-order pull)
//   T      += e / 2^kI           (rate: integral on the tick period)
//
// with per-transport gain sets (kP,kI) chosen so beta ~ alpha^2/4 keeps
// the pair near critical damping. A sustained unclamped error beyond T/2
// is a tempo *step*, not jitter: the loop reseeds the period from a
// least-squares slope over the last nine tick timestamps (unbiased under
// BLE bursts, where a period median would land on a multiple of the true
// period) and snaps the anchor. The tick count — and with it the musical
// position — carries straight through a reseed.
//
// Transport events are phase facts, not filtered inputs: Start makes the
// next tick beat 0, SPP+Continue resume at sixteenth*6 ticks, Stop
// freezes the musical position while the rate loop keeps following a
// free-running clock (many devices send 0xF8 while stopped).

#include <cstdint>

namespace neon {

class MidiClockPll {
 public:
  enum class Transport : uint8_t { kDin = 0, kUsb = 1, kBle = 2 };

  // The disciplined timeline model. Valid (returns true) once the rate
  // has been seeded; beat_valid additionally requires a live transport
  // anchor (a Start or SPP+Continue has landed on a tick and no Stop has
  // frozen it since).
  struct Model {
    uint64_t tempo_mpb_q32 = 0;      // µs per beat, Q32.32 (24 x tick period)
    int64_t origin_us = 0;           // anchor time, shared µs timebase
    int64_t beat_at_origin_q32 = 0;  // musical beat at origin_us, Q32.32
    bool playing = false;
    bool beat_valid = false;
  };

  void set_transport(Transport t) { transport_ = t; }
  Transport transport() const { return transport_; }

  // Inputs. Tick timestamps are mandatory and share the timebase the
  // rest of the module schedules in (esp_timer on ESP32 targets).
  // Transport bytes carry no time: they are ordering facts that take
  // effect on the next tick, whose own timestamp is the one that matters
  // (settled with the BLE stamp work — phases handoff §B — where only
  // ticks feed the sender-time mapper).
  void on_tick(int64_t t_us);        // 0xF8
  void on_start();                   // 0xFA: next tick is beat 0
  void on_continue();                // 0xFB: next tick resumes the position
  void on_stop();                    // 0xFC: freeze position, keep tracking
  void on_spp(uint16_t sixteenths);  // 0xF2: position := sixteenths * 6 ticks

  // True while ticks are arriving (no gap beyond 8x the tick period,
  // min 500 ms). Going inactive clears all state, transport included —
  // a sender that died mid-song must not leave a stale grid behind.
  bool active(int64_t now_us);

  // Rate seeded: the model is usable.
  bool valid() const { return have_rate_; }
  bool model(Model* out) const;

  // Residual under the lock threshold for a full beat of ticks.
  bool locked() const { return locked_; }

  bool playing() const { return playing_; }

  // Smoothed tempo (0 until seeded). The service layer owns publish
  // hysteresis; this is the continuous estimate.
  uint32_t tempo_milli_bpm() const;

  // One-shot: a Start landed on a tick — that tick's filtered time is the
  // downbeat (beat 0), the same anchoring contract RST IN uses with
  // Link's requestBeatAtTime.
  bool take_downbeat(int64_t* t_us);

  // Musical tick index at the last received 0xF8 (Start = 0), or -1
  // when the transport is not anchored. Integer beats are song_ticks
  // divisible by 24.
  int64_t song_ticks() const;
  // Filtered timestamp of that last tick (the servo's current anchor).
  int64_t last_anchor_us() const { return anchor_us_; }

  // Prediction error of the most recent tick, before it was applied.
  int64_t residual_us() const { return residual_us_; }

  uint32_t ticks() const { return ticks_; }

  void reset();

 private:
  struct Gains {
    uint32_t phase_shift;  // kP: anchor += e >> phase_shift
    uint32_t rate_shift;   // kI: period += e >> rate_shift
    uint32_t step_ticks;   // consecutive |e| > T/2 ticks that mean a step
  };
  Gains gains() const;

  bool seed_from_window(int64_t t_us);
  void apply_transport_pending(int64_t t_us);
  int64_t predict(int64_t tick) const;
  int64_t period_us() const;
  int64_t window_slope_us() const;
  // max/min consecutive interval in the seed window; 1 if too short.
  int64_t window_interval_ratio() const;

  Transport transport_ = Transport::kDin;

  // Recent tick timestamps (consecutive ticks), for seeding and step
  // reseeds via a least-squares slope.
  static constexpr uint32_t kWindow = 9;
  static constexpr int64_t kMaxPeriodUs = 500000;   // < 10 BPM: restart
  static constexpr int64_t kMinTimeoutUs = 500000;
  static constexpr uint32_t kLockTicks = 24;        // one beat
  static constexpr int64_t kMinTickUs = 2500;       // 999 BPM (PI clamp)
  static constexpr int64_t kMinSeedTickUs = 8000;   // 312 BPM: reject FIFO dumps
  static constexpr int64_t kMaxTickUs = 125000;     // 20 BPM
  int64_t times_[kWindow] = {};
  uint32_t time_count_ = 0;
  uint32_t time_next_ = 0;

  int64_t last_tick_us_ = INT64_MIN;

  // The servo: anchor maps pll tick index -> µs; period is Q32.32.
  bool have_rate_ = false;
  int64_t anchor_tick_ = 0;
  int64_t anchor_us_ = 0;
  uint64_t us_per_tick_q32_ = 0;
  int64_t pll_tick_ = -1;  // index of the last received tick
  int64_t residual_us_ = 0;
  uint32_t ticks_ = 0;

  bool locked_ = false;
  uint32_t lock_streak_ = 0;
  uint32_t step_streak_ = 0;

  // Musical position. song_offset_ maps pll ticks onto song ticks while
  // anchored; resume_song_tick_ is where the next tick lands after a
  // Start/Continue consumes its pending flag.
  bool playing_ = false;
  bool beat_anchored_ = false;
  int64_t song_offset_ = 0;
  int64_t resume_song_tick_ = 0;
  bool pending_start_ = false;
  bool pending_continue_ = false;

  bool downbeat_pending_ = false;
  int64_t downbeat_us_ = 0;
};

}  // namespace neon
