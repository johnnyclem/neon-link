#include <doctest.h>

#include <cstdint>

#include "neon/fixed_math.hpp"

namespace {

// Deterministic 64-bit PRNG (splitmix64) — no global RNG state, seeded.
uint64_t splitmix64(uint64_t& s) {
  s += 0x9e3779b97f4a7c15ull;
  uint64_t z = s;
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
  return z ^ (z >> 31);
}

}  // namespace

TEST_CASE("mul_u64: known values") {
  auto r = neon::mul_u64(0, 0xffffffffffffffffull);
  CHECK(r.hi == 0);
  CHECK(r.lo == 0);

  r = neon::mul_u64(0xffffffffffffffffull, 0xffffffffffffffffull);
  CHECK(r.hi == 0xfffffffffffffffeull);
  CHECK(r.lo == 1);

  r = neon::mul_u64(1ull << 32, 1ull << 32);
  CHECK(r.hi == 1);
  CHECK(r.lo == 0);

  r = neon::mul_u64(500000ull << 32, 4);
  CHECK(r.hi == 0);
  CHECK(r.lo == 2000000ull << 32);
}

TEST_CASE("div_u128_u64: known values") {
  // (1 << 96) / (1 << 40) == 1 << 56
  CHECK(neon::div_u128_u64(neon::U128{1ull << 32, 0}, 1ull << 40) ==
        1ull << 56);
  // hi == 0 path
  CHECK(neon::div_u128_u64(neon::U128{0, 1000000007ull}, 97) ==
        1000000007ull / 97);
  // saturation when the quotient would overflow
  CHECK(neon::div_u128_u64(neon::U128{5, 0}, 5) == UINT64_MAX);
  CHECK(neon::div_u128_u64(neon::U128{6, 0}, 5) == UINT64_MAX);
}

#ifdef __SIZEOF_INT128__
TEST_CASE("mul_u64 and div_u128_u64 match __int128 oracle") {
  uint64_t seed = 0x5eed5eed5eed5eedull;
  for (int i = 0; i < 20000; ++i) {
    const uint64_t a = splitmix64(seed);
    const uint64_t b = splitmix64(seed);

    const unsigned __int128 p =
        static_cast<unsigned __int128>(a) * static_cast<unsigned __int128>(b);
    const auto r = neon::mul_u64(a, b);
    CHECK(r.hi == static_cast<uint64_t>(p >> 64));
    CHECK(r.lo == static_cast<uint64_t>(p));

    // Division with in-range quotient: n = (hi:lo) with hi < d.
    uint64_t d = splitmix64(seed);
    if (d == 0) {
      d = 1;
    }
    const uint64_t hi = d > 1 ? splitmix64(seed) % d : 0;
    const uint64_t lo = splitmix64(seed);
    const unsigned __int128 n =
        (static_cast<unsigned __int128>(hi) << 64) | lo;
    CHECK(neon::div_u128_u64(neon::U128{hi, lo}, d) ==
          static_cast<uint64_t>(n / d));
  }
}
#endif

TEST_CASE("q32_div: identity and halving") {
  // (5 << 32) / (2 << 32) == 2.5 in Q32.32
  CHECK(neon::q32_div(5ull << 32, 2ull << 32) == (5ull << 31));
  // x / x == 1.0
  const uint64_t x = 0x123456789abcull;
  CHECK(neon::q32_div(x, x) == (1ull << 32));
}
