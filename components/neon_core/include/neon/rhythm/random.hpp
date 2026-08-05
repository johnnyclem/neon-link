#pragma once

#include <cstdint>

namespace neon {

// Deterministic per-tick hash (splitmix64 finalizer): probability and
// humanize decisions are pure functions of the absolute tick index, so
// patterns are reproducible across re-anchors and identical on every
// unit in a session.
inline uint64_t tick_hash(uint64_t tick_index) {
  uint64_t z = tick_index + 0x9e3779b97f4a7c15ull;
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
  return z ^ (z >> 31);
}

// True with probability pct/100 for this tick.
inline bool probability_hit(uint64_t tick_index, uint8_t pct) {
  if (pct >= 100) {
    return true;
  }
  if (pct == 0) {
    return false;
  }
  return tick_hash(tick_index) % 100 < pct;
}

}  // namespace neon
