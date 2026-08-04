#pragma once

// Portable 64×64→128-bit helpers for the integer beat math. The ESP32-S3
// toolchain has no native 128-bit integer type, so products and wide
// divisions are done with 32-bit limbs. Host tests cross-check these
// against __int128 where available. None of this runs in the ISR; it is
// used when (re)anchoring the beat grid.

#include <cstdint>

namespace neon {

struct U128 {
  uint64_t hi;
  uint64_t lo;
};

inline U128 mul_u64(uint64_t a, uint64_t b) {
  const uint64_t a_lo = a & 0xffffffffull;
  const uint64_t a_hi = a >> 32;
  const uint64_t b_lo = b & 0xffffffffull;
  const uint64_t b_hi = b >> 32;

  const uint64_t p0 = a_lo * b_lo;
  const uint64_t p1 = a_lo * b_hi;
  const uint64_t p2 = a_hi * b_lo;
  const uint64_t p3 = a_hi * b_hi;

  const uint64_t mid = (p0 >> 32) + (p1 & 0xffffffffull) + (p2 & 0xffffffffull);

  U128 r;
  r.lo = (mid << 32) | (p0 & 0xffffffffull);
  r.hi = p3 + (p1 >> 32) + (p2 >> 32) + (mid >> 32);
  return r;
}

// (n.hi:n.lo) / d. Saturates to UINT64_MAX if the quotient does not fit
// in 64 bits (callers keep operands in range; saturation is a guard, not
// an expected path). d must be non-zero.
inline uint64_t div_u128_u64(U128 n, uint64_t d) {
  if (n.hi == 0) {
    return n.lo / d;
  }
  if (n.hi >= d) {
    return UINT64_MAX;  // quotient >= 2^64
  }
  uint64_t q = 0;
  uint64_t r = n.hi;
  for (int i = 63; i >= 0; --i) {
    const uint64_t bit = (n.lo >> i) & 1u;
    // r = r*2 + bit, tracking the bit shifted out of the top. When the
    // doubled value exceeds 64 bits the unsigned wrap of (r<<1|bit) - d is
    // still the correct remainder because the true value is 2^64 + wrapped.
    const bool carry = (r >> 63) != 0;
    r = (r << 1) | bit;
    if (carry || r >= d) {
      r -= d;
      q |= 1ull << i;
    }
  }
  return q;
}

// Q32.32 division: (a << 32) / b.
inline uint64_t q32_div(uint64_t a, uint64_t b) {
  const U128 n{a >> 32, a << 32};
  return div_u128_u64(n, b);
}

}  // namespace neon
