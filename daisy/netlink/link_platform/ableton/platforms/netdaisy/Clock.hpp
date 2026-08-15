// Link clock on the Teensy's shared 64-bit microsecond timebase — the
// same domain the pulse engine, MIDI clock, and UI schedule in, which
// is what makes captured session state directly usable by all of them.

#pragma once

#include <chrono>
#include <cstdint>

// daisy/src/timebase_daisy.cpp
int64_t daisy_now_us();

namespace ableton
{
namespace platforms
{
namespace netdaisy
{

struct Clock
{
  std::chrono::microseconds micros() const
  {
    return std::chrono::microseconds{daisy_now_us()};
  }
};

} // namespace netdaisy
} // namespace platforms
} // namespace ableton
