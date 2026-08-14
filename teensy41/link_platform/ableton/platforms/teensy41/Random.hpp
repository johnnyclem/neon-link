// Session/node id entropy. xorshift32 seeded from the cycle counter and
// the microsecond clock on first use — ids must differ between boots
// and between peers, and this target has no hardware TRNG driver.

#pragma once

#include <cstdint>

int64_t t41_now_us();

namespace ableton
{
namespace platforms
{
namespace teensy41
{

struct Random
{
  uint8_t operator()()
  {
    static uint32_t state = 0;
    if (state == 0)
    {
      volatile uint32_t* const cyccnt = reinterpret_cast<uint32_t*>(0xE0001004);
      state = static_cast<uint32_t>(t41_now_us()) ^ *cyccnt ^ 0x9E3779B9u;
      if (state == 0)
      {
        state = 1;
      }
    }
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return static_cast<uint8_t>((state % 93) + 33); // printable ascii
  }
};

} // namespace teensy41
} // namespace platforms
} // namespace ableton
