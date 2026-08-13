#pragma once

#include <cstdint>

namespace neon::client {

// Coalesce bursts (Live automation ramps) into at most one fire per
// `min_interval_ms`. First event after a quiet period fires immediately.
class Coalesce {
 public:
  explicit Coalesce(int min_interval_ms = 100);

  // Record that a change arrived at `now_ms`. Returns true if a send
  // should happen now (first event, or interval elapsed).
  bool note(int64_t now_ms);

  int min_interval_ms() const { return min_interval_ms_; }

 private:
  int min_interval_ms_;
  int64_t last_fire_ms_ = -1;
};

}  // namespace neon::client
