// Daisy netlink shadow of ableton/platforms/Config.hpp: selects the
// lwIP-backed polled platform unconditionally — this include
// directory only exists on the netdaisy build.

#pragma once

#include <ableton/platforms/netdaisy/Clock.hpp>
#include <ableton/platforms/netdaisy/Context.hpp>
#include <ableton/platforms/netdaisy/Random.hpp>
#include <ableton/platforms/netdaisy/ScanIpIfAddrs.hpp>
#include <ableton/util/Log.hpp>

namespace ableton
{
namespace link
{
namespace platform
{

using Clock = platforms::netdaisy::Clock;
using IoContext =
  platforms::netdaisy::Context<platforms::netdaisy::ScanIpIfAddrs, util::NullLog>;
using Random = platforms::netdaisy::Random;

} // namespace platform
} // namespace link
} // namespace ableton
