#include "neon/link_snapshot.hpp"

#include <cmath>

namespace neon {

namespace {
constexpr double kQ32 = 4294967296.0;  // 2^32

double clamp_bpm(double bpm) {
  if (!(bpm > 1.0)) {
    return 1.0;  // also catches NaN
  }
  if (bpm > 999.0) {
    return 999.0;
  }
  return bpm;
}
}  // namespace

bool build_snapshot(const hal::LinkState& state, const TimelineSnapshot* prev,
                    TimelineSnapshot& out) {
  const double bpm = clamp_bpm(state.tempo_bpm);
  const double mpb_us = 60000000.0 / bpm;

  out.tempo_mpb_q32 = static_cast<uint64_t>(mpb_us * kQ32 + 0.5);
  out.origin_us = state.origin_us;
  out.beat_at_origin_q32 =
      static_cast<int64_t>(std::llround(state.beat_at_origin * kQ32));
  out.quantum_beats =
      state.quantum >= 1.0 ? static_cast<uint32_t>(state.quantum) : 1u;
  out.playing = state.playing ? 1 : 0;
  out.num_peers = state.num_peers;
  out.latency_us = prev ? prev->latency_us : 0;

  if (prev == nullptr) {
    return true;
  }
  if (prev->playing != out.playing || prev->num_peers != out.num_peers ||
      prev->quantum_beats != out.quantum_beats) {
    return true;
  }

  // Tempo: relative difference in µs-per-beat ≈ relative BPM difference.
  const double prev_mpb = static_cast<double>(prev->tempo_mpb_q32) / kQ32;
  if (std::fabs(mpb_us - prev_mpb) > prev_mpb * 5e-5) {
    return true;
  }

  // Phase: where does `prev` predict the beat to be at the new origin?
  const double predicted =
      static_cast<double>(prev->beat_at_origin_q32) / kQ32 +
      static_cast<double>(out.origin_us - prev->origin_us) / prev_mpb;
  if (std::fabs(predicted - state.beat_at_origin) > kBeatEpsilon) {
    return true;
  }
  return false;
}

}  // namespace neon
