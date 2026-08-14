// UDP socket with the ableton::platforms::asio::Socket surface, over a
// QNEthernet EthernetUDP owned by the polled Runtime. receive() arms a
// one-shot handler; the Runtime delivers the next packet to it from
// poll(), on the main thread.

#pragma once

#include <ableton/discovery/AsioTypes.hpp>
#include <ableton/platforms/teensy41/Runtime.hpp>

#include <cassert>
#include <memory>
#include <utility>

namespace ableton
{
namespace platforms
{
namespace teensy41
{

template <std::size_t MaxPacketSize>
struct Socket
{
  explicit Socket(std::shared_ptr<SocketState> state)
    : mpImpl(std::move(state))
  {
  }

  Socket(const Socket&) = delete;
  Socket& operator=(const Socket&) = delete;
  Socket(Socket&& rhs)
    : mpImpl(std::move(rhs.mpImpl))
  {
  }

  std::size_t send(
    const uint8_t* const pData, const size_t numBytes, const discovery::UdpEndpoint& to)
  {
    assert(numBytes <= MaxPacketSize);
    return Runtime::send(*mpImpl, pData, numBytes, to);
  }

  template <typename Handler>
  void receive(Handler handler)
  {
    mpImpl->handler = [handler](const discovery::UdpEndpoint& from,
                        const uint8_t* begin, const uint8_t* end) mutable
    {
      if (static_cast<std::size_t>(end - begin) <= MaxPacketSize)
      {
        handler(from, begin, end);
      }
    };
  }

  discovery::UdpEndpoint endpoint() const { return mpImpl->local; }

  std::shared_ptr<SocketState> mpImpl;
};

} // namespace teensy41
} // namespace platforms
} // namespace ableton
