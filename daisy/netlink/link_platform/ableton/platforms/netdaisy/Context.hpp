// The Teensy 4.1 IoContext: hands out polled timers and QNEthernet
// sockets from the global Runtime, and runs posted jobs from the main
// loop's pump. Mirrors the esp32 platform Context's surface with the
// FreeRTOS service task replaced by neon_netlink_link_poll().

#pragma once

#include <ableton/discovery/AsioTypes.hpp>
#include <ableton/platforms/netdaisy/Dispatcher.hpp>
#include <ableton/platforms/netdaisy/Runtime.hpp>
#include <ableton/platforms/netdaisy/Socket.hpp>
#include <ableton/platforms/netdaisy/Timer.hpp>

#include <functional>
#include <utility>
#include <vector>

namespace ableton
{
namespace platforms
{
namespace netdaisy
{

template <typename ScanIpIfAddrs, typename LogT>
class Context
{
public:
  using Timer = PolledTimer;
  using Log = LogT;

  template <typename Handler, typename Duration>
  using LockFreeCallbackDispatcher = SyncCallbackDispatcher<Handler, Duration>;

  template <std::size_t BufferSize>
  using Socket = netdaisy::Socket<BufferSize>;

  Context() = default;

  // Controller hands the context a UDP-send exception handler; sends in
  // this platform report failure by return value, so it is unused.
  template <typename ExceptionHandler>
  explicit Context(ExceptionHandler)
  {
  }

  Context(const Context&) = delete;
  Context(Context&& rhs)
    : mLog(std::move(rhs.mLog))
    , mScanIpIfAddrs(std::move(rhs.mScanIpIfAddrs))
  {
  }

  void stop() {}

  template <std::size_t BufferSize>
  Socket<BufferSize> openUnicastSocket(const discovery::IpAddress& addr)
  {
    return Socket<BufferSize>{Runtime::instance().openSocket(addr, false)};
  }

  template <std::size_t BufferSize>
  Socket<BufferSize> openMulticastSocket(const discovery::IpAddress& addr)
  {
    return Socket<BufferSize>{Runtime::instance().openSocket(addr, true)};
  }

  std::vector<discovery::IpAddress> scanNetworkInterfaces()
  {
    return mScanIpIfAddrs();
  }

  Timer makeTimer() const { return {}; }

  template <typename Handler>
  void async(Handler handler)
  {
    Runtime::instance().post(std::move(handler));
  }

  Log& log() { return mLog; }

private:
  Log mLog;
  ScanIpIfAddrs mScanIpIfAddrs;
};

} // namespace netdaisy
} // namespace platforms
} // namespace ableton
