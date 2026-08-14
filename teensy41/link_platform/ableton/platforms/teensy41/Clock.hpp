// Link clock on the Teensy's shared 64-bit microsecond timebase — the
// same domain the pulse engine, MIDI clock, and UI schedule in, which
// is what makes captured session state directly usable by all of them.

#pragma once

#include <chrono>
#include <cstdint>

// teensy41/src/timebase_t41.cpp
int64_t t41_now_us();

namespace ableton
{
namespace platforms
{
namespace teensy41
{

struct Clock
{
  std::chrono::microseconds micros() const
  {
    return std::chrono::microseconds{t41_now_us()};
  }
};

} // namespace teensy41
} // namespace platforms
} // namespace ableton
