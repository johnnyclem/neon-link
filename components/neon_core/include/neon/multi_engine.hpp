#pragma once

#include <cstddef>
#include <cstdint>

#include "neon/output_config.hpp"
#include "neon/pulse_channel.hpp"
#include "neon/timeline.hpp"

namespace neon {

// Channel indices in the merged edge stream.
enum Channel : uint8_t {
  kChClk1 = 0,
  kChClk2 = 1,
  kChClk3 = 2,
  kChClk4 = 3,
  kChReset = 4,
  kChRun = 5,
  kChannelCount = 6,
};

struct EngineConfig {
  ClockOutputConfig clocks[4];
  ResetMode reset_mode = ResetMode::kStartOfPlay;
  uint32_t reset_trig_len_us = 5000;
  bool run_enabled = true;
  // When true, clock outputs stop while the session transport is stopped;
  // when false they free-run on the beat grid (Run/Reset always follow
  // transport either way). A per-output free_run overrides this.
  bool transport_gating = false;
  int32_t latency_us = 0;

  // Reset alignment: legacy boxes let the loop-reset trigger sit exactly
  // on the clock edge, or lead it slightly so a sequencer latches the
  // reset before the clock that starts the loop. reset_lead_us is only
  // applied when reset_before_edge is set.
  bool reset_before_edge = false;
  uint32_t reset_lead_us = 1000;

  // Quantum used to derive loop-length rates (bar reset, and rhythm
  // patterns configured with rhythm_over_loop). Kept in sync with the
  // timeline on every retime.
  uint32_t quantum_beats = 4;

  EngineConfig() {
    // Defaults: 16ths, 8ths, quarters, and MIDI-clock rate.
    clocks[0].ppqn = 4;
    clocks[1].ppqn = 2;
    clocks[2].ppqn = 1;
    clocks[3].ppqn = 24;
  }
};

// The full output engine: four role-assignable outputs, a dedicated Reset
// pulse, a Run gate, and shared latency compensation. Owns per-channel
// PulseChannels and merges their edges in time order. Same consumption
// contract as milestone 2: generate() over contiguous half-open windows;
// edges before t0 are consumed silently.
class MultiClockEngine {
 public:
  void set_config(const EngineConfig& cfg);
  const EngineConfig& config() const { return cfg_; }

  // Re-anchor everything to a new timeline snapshot. Transport
  // transitions (playing flips) emit Run edges, Gate-role edges, and the
  // one-shot Reset pulses (start-of-play / at-stop) at from_us.
  void retime(const TimelineSnapshot& tl, int64_t from_us);

  size_t generate(int64_t t0_us, int64_t t1_us, Edge* out, size_t max_out);

  bool run_level() const { return run_level_; }

 private:
  // Effective config for output i once role / rhythm_over_loop are folded
  // in (roles other than kClock become loop-rate trigger channels).
  ClockOutputConfig effective_clock(int index, uint32_t quantum) const;
  // Is output i a free-running pulse source right now?
  bool channel_runs(int index) const;
  void push_pending(int64_t t_us, uint8_t channel, bool high);
  void emit_reset_pulse(int64_t t_us, uint8_t channel, uint32_t len_us);
  int32_t reset_latency() const;
  // Drive every level-holding channel (Gate roles, RUN) to the level its
  // current config and transport state call for. Runs on every retime, so
  // a role or enable change takes effect immediately instead of waiting
  // for the next transport transition.
  void sync_levels(int64_t from_us);

  EngineConfig cfg_{};
  TimelineSnapshot tl_{};
  bool have_timeline_ = false;

  PulseChannel clocks_[4];
  PulseChannel reset_bar_;  // dedicated RESET jack in kEveryBar mode

  // One-shot edges (transport transitions, reset pulses), already in the
  // post-latency output timebase.
  static constexpr size_t kMaxPending = 24;
  struct Pending {
    int64_t t_us;
    uint8_t channel;
    bool high;
    bool valid;
  };
  Pending pending_[kMaxPending] = {};

  // Last level *scheduled* for each level-holding channel, so sync_levels
  // only emits an edge when the target actually moves. Outputs start low,
  // which is the hardware's power-on state.
  bool gate_level_[4] = {false, false, false, false};
  bool run_target_ = false;

  bool run_level_ = false;
  bool playing_ = false;
};

}  // namespace neon
