#pragma once

#include <cstdint>

namespace neon {

// Euclidean rhythm membership: distributes `fills` hits as evenly as
// possible across `steps` positions (Bresenham form of the Bjorklund
// algorithm; hit at step 0 when fills > 0 and rot == 0). `rot` rotates
// the pattern forward.
inline bool euclid_hit(uint32_t step, uint32_t steps, uint32_t fills,
                       uint32_t rot) {
  if (steps == 0 || fills == 0) {
    return false;
  }
  if (fills >= steps) {
    return true;
  }
  const uint64_t i = (static_cast<uint64_t>(step) + rot) % steps;
  return (i * fills) % steps < fills;
}

}  // namespace neon
