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
  // transport either way).
  bool transport_gating = false;
  int32_t latency_us = 0;

  EngineConfig() {
    // Defaults: 16ths, 8ths, quarters, and MIDI-clock rate.
    clocks[0].ppqn = 4;
    clocks[1].ppqn = 2;
    clocks[2].ppqn = 1;
    clocks[3].ppqn = 24;
  }
};

// The full output engine: four clocks, Reset pulse, Run gate, shared
// latency compensation. Owns per-channel PulseChannels and merges their
// edges in time order. Same consumption contract as milestone 2:
// generate() over contiguous half-open windows; edges before t0 are
// consumed silently.
class MultiClockEngine {
 public:
  void set_config(const EngineConfig& cfg);
  const EngineConfig& config() const { return cfg_; }

  // Re-anchor everything to a new timeline snapshot. Transport
  // transitions (playing flips) emit Run edges and StartOfPlay Reset
  // pulses at from_us.
  void retime(const TimelineSnapshot& tl, int64_t from_us);

  size_t generate(int64_t t0_us, int64_t t1_us, Edge* out, size_t max_out);

  bool run_level() const { return run_level_; }

 private:
  void apply_config(int64_t from_us);

  EngineConfig cfg_{};
  TimelineSnapshot tl_{};
  bool have_timeline_ = false;

  PulseChannel clocks_[4];
  PulseChannel reset_bar_;  // active in kEveryBar mode

  // One-shot pending edges (transport transitions), pre-latency times.
  static constexpr int64_t kNone = INT64_MIN;
  int64_t pending_run_edge_ = kNone;
  bool pending_run_high_ = false;
  int64_t pending_reset_rise_ = kNone;
  int64_t pending_reset_fall_ = kNone;

  bool run_level_ = false;
  bool playing_ = false;
};

}  // namespace neon
