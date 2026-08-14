// Teensy 4.1 shadow of ableton/platforms/Config.hpp: selects the
// QNEthernet-backed polled platform unconditionally — this include
// directory only exists on the teensy41 build.

#pragma once

#include <ableton/platforms/teensy41/Clock.hpp>
#include <ableton/platforms/teensy41/Context.hpp>
#include <ableton/platforms/teensy41/Random.hpp>
#include <ableton/platforms/teensy41/ScanIpIfAddrs.hpp>
#include <ableton/util/Log.hpp>

namespace ableton
{
namespace link
{
namespace platform
{

using Clock = platforms::teensy41::Clock;
using IoContext =
  platforms::teensy41::Context<platforms::teensy41::ScanIpIfAddrs, util::NullLog>;
using Random = platforms::teensy41::Random;

} // namespace platform
} // namespace link
} // namespace ableton
