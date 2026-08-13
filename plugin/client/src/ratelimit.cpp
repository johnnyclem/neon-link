#include "neon/client/ratelimit.hpp"

namespace neon::client {

Coalesce::Coalesce(int min_interval_ms) : min_interval_ms_(min_interval_ms) {}

bool Coalesce::note(int64_t now_ms) {
  if (last_fire_ms_ < 0 || now_ms - last_fire_ms_ >= min_interval_ms_) {
    last_fire_ms_ = now_ms;
    return true;
  }
  return false;
}

}  // namespace neon::client
