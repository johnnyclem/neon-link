// Teensy 4.1 shadow of ableton/platforms/asio/AsioWrapper.hpp.
//
// Upstream, this header pulls in standalone asio. The Teensy build has
// no asio; the only protocol header that includes this directly
// (link/EndpointV6.hpp) needs nothing beyond the discovery type
// aliases, which the shadowed AsioTypes.hpp provides asio-free.

#pragma once

#include <ableton/platforms/teensy41/NetTypes.hpp>
