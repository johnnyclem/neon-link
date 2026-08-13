#include "neon/audio/pulse_render.hpp"

#include "neon/audio/types.hpp"
#include "neon/fixed_math.hpp"

namespace neon {

void PulseRender::reset(uint32_t sample_rate) {
  sample_rate_ = sample_rate != 0 ? sample_rate : 44100;
  frames_ = 0;
  dropped_ = 0;
  for (uint8_t c = 0; c < kChannelCount; ++c) {
    count_[c] = 0;
    start_level_[c] = false;
    end_level_[c] = false;
  }
}

uint8_t PulseRender::channel_for_role(uint8_t role) {
  switch (static_cast<AudioRole>(role)) {
    case AudioRole::kClock:
      return kChClk1;
    case AudioRole::kReset:
      return kChReset;
    case AudioRole::kRun:
      return kChRun;
    default:
      return kChannelCount;
  }
}

void PulseRender::begin_block(int64_t t0_us, int64_t t1_us, uint32_t frames) {
  frames_ = frames;
  for (uint8_t c = 0; c < kChannelCount; ++c) {
    count_[c] = 0;
    start_level_[c] = end_level_[c];
  }
  if (frames == 0 || t1_us <= t0_us) {
    return;
  }

  Edge buf[kMaxEdges];
  const size_t n = engine_.generate(t0_us, t1_us, buf, kMaxEdges);
  const uint64_t span = static_cast<uint64_t>(t1_us - t0_us);
  for (size_t i = 0; i < n; ++i) {
    const Edge& e = buf[i];
    if (e.channel >= kChannelCount) {
      continue;
    }
    int64_t d = e.t_us - t0_us;
    if (d < 0) {
      d = 0;
    }
    uint64_t f =
        div_u128_u64(mul_u64(static_cast<uint64_t>(d), frames), span);
    if (f >= frames) {
      f = frames - 1;
    }
    uint32_t& c = count_[e.channel];
    if (c >= kMaxEdgesPerChannel) {
      ++dropped_;
      end_level_[e.channel] = e.high;  // keep the level truthful
      continue;
    }
    edges_[e.channel][c].frame = static_cast<uint32_t>(f);
    edges_[e.channel][c].high = e.high;
    ++c;
    end_level_[e.channel] = e.high;
  }
}

uint32_t PulseRender::render_channel(uint8_t channel, float level, float* out,
                                     uint32_t frames) const {
  if (channel >= kChannelCount || out == nullptr || frames == 0) {
    return 0;
  }
  bool high = start_level_[channel];
  uint32_t next = 0;
  const uint32_t n = count_[channel];
  for (uint32_t i = 0; i < frames; ++i) {
    while (next < n && edges_[channel][next].frame == i) {
      high = edges_[channel][next].high;
      ++next;
    }
    if (high) {
      out[i] += level;
    }
  }
  return n;
}

}  // namespace neon
