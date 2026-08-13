#pragma once

// Clock / reset / run as audio.
//
// The point of this file is that it does *not* re-derive the pulse grid.
// It drives the same MultiClockEngine the GPIO path uses, over the same
// contiguous time windows, and converts the edges it emits into sample
// offsets inside the block. Whatever the jacks are doing, the audio
// channels do — to within one sample (~23 µs at 44.1 kHz, against ~1 ms on
// the I2C CV path).

#include <cstddef>
#include <cstdint>

#include "neon/multi_engine.hpp"
#include "neon/timeline.hpp"

namespace neon {

class PulseRender {
 public:
  void reset(uint32_t sample_rate);
  void set_config(const EngineConfig& cfg) { engine_.set_config(cfg); }
  void retime(const TimelineSnapshot& tl, int64_t from_us) {
    engine_.retime(tl, from_us);
  }

  // Consume one block's worth of the edge stream. [t0_us, t1_us) must be
  // contiguous with the previous call — this is the same consumption
  // contract MultiClockEngine already has.
  void begin_block(int64_t t0_us, int64_t t1_us, uint32_t frames);

  // Write channel `channel`'s gate signal into out[frames] as 0 / +level,
  // starting from the level held at the end of the previous block. Adds
  // into the buffer. Returns the number of edges placed in this block.
  uint32_t render_channel(uint8_t channel, float level, float* out,
                          uint32_t frames) const;

  bool level(uint8_t channel) const {
    return channel < kChannelCount ? end_level_[channel] : false;
  }

  // Edge placement for the current block, for tests and for latency work.
  uint32_t edge_count(uint8_t channel) const {
    return channel < kChannelCount ? count_[channel] : 0;
  }
  uint32_t edge_frame(uint8_t channel, uint32_t index) const {
    return channel < kChannelCount && index < count_[channel]
               ? edges_[channel][index].frame
               : 0;
  }
  bool edge_high(uint8_t channel, uint32_t index) const {
    return channel < kChannelCount && index < count_[channel] &&
           edges_[channel][index].high;
  }

  // The channel index an audio role taps, or kChannelCount for roles that
  // are not pulse sources.
  static uint8_t channel_for_role(uint8_t role);

 private:
  static constexpr uint32_t kMaxEdgesPerChannel = 16;
  static constexpr size_t kMaxEdges = 64;

  struct FrameEdge {
    uint32_t frame;
    bool high;
  };

  MultiClockEngine engine_;
  uint32_t sample_rate_ = 44100;
  uint32_t frames_ = 0;

  FrameEdge edges_[kChannelCount][kMaxEdgesPerChannel] = {};
  uint32_t count_[kChannelCount] = {};
  bool start_level_[kChannelCount] = {};
  bool end_level_[kChannelCount] = {};
  uint32_t dropped_ = 0;

 public:
  // Edges that did not fit the per-block budget (never expected; a
  // non-zero value means the grid is far denser than audio can render).
  uint32_t dropped() const { return dropped_; }
};

}  // namespace neon
