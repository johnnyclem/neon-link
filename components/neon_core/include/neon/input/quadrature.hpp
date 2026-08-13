#pragma once

#include <cstdint>

namespace neon {

// Gray-code quadrature. AB is packed as (A << 1) | B, each bit 0 or 1.
// Returns -1, 0, or +1 for one legal transition; 0 on a no-op or a skipped
// state (00 <-> 11), which is what a missed sample looks like.
inline int gray_step(unsigned old_ab, unsigned new_ab) {
  static constexpr int8_t kStep[16] = {
      0,  +1, -1, 0,  // 00 -> 00,01,10,11
      -1, 0,  0,  +1,  // 01 -> 00,01,10,11
      +1, 0,  0,  -1,  // 10 -> 00,01,10,11
      0,  -1, +1, 0,   // 11 -> 00,01,10,11
  };
  return kStep[((old_ab & 3u) << 2) | (new_ab & 3u)];
}

// ×4 decode: one detent is a full gray cycle (4 steps), matching the
// PCNT encoder path on the custom PCB.
class QuadDecoder {
 public:
  void reset(unsigned ab = 0) {
    last_ab_ = ab & 3u;
    rem_ = 0;
  }

  // Feed a new AB sample. Returns whole detents produced by this sample
  // (usually 0, occasionally ±1).
  int feed(unsigned ab) {
    ab &= 3u;
    rem_ += gray_step(last_ab_, ab);
    last_ab_ = ab;
    const int detents = rem_ / 4;
    rem_ %= 4;
    return detents;
  }

  unsigned last_ab() const { return last_ab_; }

 private:
  unsigned last_ab_ = 0;
  int rem_ = 0;
};

}  // namespace neon
