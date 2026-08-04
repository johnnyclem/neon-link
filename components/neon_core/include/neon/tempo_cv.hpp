#pragma once

#include <cstdint>

namespace neon {

// Tempo CV mapping: BPM -> output ratio in Q16 (0..65535 spans 0..5 V).
// Linear between the configured min/max BPM, clamped outside.
inline uint16_t tempo_cv_ratio_q16(uint32_t milli_bpm, uint16_t min_bpm,
                                   uint16_t max_bpm) {
  if (max_bpm <= min_bpm) {
    return 0;
  }
  const int64_t lo = static_cast<int64_t>(min_bpm) * 1000;
  const int64_t hi = static_cast<int64_t>(max_bpm) * 1000;
  int64_t v = static_cast<int64_t>(milli_bpm);
  if (v <= lo) return 0;
  if (v >= hi) return 65535;
  return static_cast<uint16_t>((v - lo) * 65535 / (hi - lo));
}

}  // namespace neon
