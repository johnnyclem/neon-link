#pragma once

// Outbound OSC binding policy. Pure logic, host-tested; the service
// samples sources, asks each binding whether a send is due, and
// reports back whether the datagram actually left. Semantics carried
// over from the SolarOS evaluation (docs/SOLAROS_PORTS_HANDOFF.md §4):
//
//  - Sampling cadence per binding (interval_ms).
//  - Scalar delta gate compares against the last *sent* value, not the
//    last sampled one, so a slow drift still eventually crosses the
//    threshold instead of being filtered forever.
//  - Event edge filter fires on level transitions of (v != 0), never
//    on the first observation.
//  - Two-phase commit: prepare() stashes the candidate; only
//    note_sent() — called after a successful sendto — promotes it, so
//    a failed send never suppresses the retry.

#include <cmath>
#include <cstdint>

namespace neon {
namespace osc {

class OutBinding {
 public:
  enum class Kind : uint8_t { kScalar, kEvent };
  enum class Edge : uint8_t { kRising, kFalling, kBoth };

  void configure(Kind kind, int32_t interval_ms, float delta,
                 Edge edge = Edge::kBoth) {
    kind_ = kind;
    interval_us_ = static_cast<int64_t>(interval_ms) * 1000;
    delta_ = delta;
    edge_ = edge;
  }

  bool due(int64_t now_us) const {
    return last_sample_us_ < 0 || now_us - last_sample_us_ >= interval_us_;
  }

  // Feed one sample; true = a send should be attempted with pending().
  bool prepare(int64_t now_us, float value) {
    last_sample_us_ = now_us;
    if (kind_ == Kind::kEvent) {
      const bool level = value != 0.0f;
      const bool had = had_value_;
      const bool prev = last_level_;
      had_value_ = true;
      last_level_ = level;
      if (!had || level == prev) {
        return false;
      }
      const bool rising = level;
      if ((rising && edge_ == Edge::kFalling) ||
          (!rising && edge_ == Edge::kRising)) {
        return false;
      }
      pending_ = level ? 1.0f : 0.0f;
      return true;
    }
    if (has_sent_) {
      const bool past_delta = delta_ > 0.0f
                                  ? std::fabs(value - last_sent_) >= delta_
                                  : value != last_sent_;
      if (!past_delta) {
        return false;
      }
    }
    pending_ = value;
    return true;
  }

  float pending() const { return pending_; }

  void note_sent() {
    has_sent_ = true;
    last_sent_ = pending_;
  }

 private:
  Kind kind_ = Kind::kScalar;
  Edge edge_ = Edge::kBoth;
  int64_t interval_us_ = 0;
  int64_t last_sample_us_ = -1;
  float delta_ = 0.0f;
  float pending_ = 0.0f;
  float last_sent_ = 0.0f;
  bool has_sent_ = false;
  bool had_value_ = false;
  bool last_level_ = false;
};

}  // namespace osc
}  // namespace neon
