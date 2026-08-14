// The polled io runtime behind the Teensy Link platform: one global
// registry of timers, UDP sockets, and posted jobs, pumped from the
// Arduino loop by neon_t41_link_poll(). Single-threaded by design —
// every Link call and every handler runs on the main thread, which is
// what lets the std::mutex shim be a no-op.

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include <ableton/discovery/AsioTypes.hpp>

int64_t t41_now_us();

namespace ableton
{
namespace platforms
{
namespace teensy41
{

struct TimerState
{
  int64_t dueUs = 0;
  bool armed = false;
  std::function<void(::neonnet::error_code)> handler;
};

// Type-erased socket plumbing. The QNEthernet-facing half lives in
// LinkRuntime.cpp so this header stays includable without Arduino.
struct SocketState
{
  // Filled by the platform-specific open call.
  ::ableton::discovery::UdpEndpoint local;
  std::function<void(const ::ableton::discovery::UdpEndpoint&, const uint8_t*,
    const uint8_t*)>
    handler;
  void* udp = nullptr; // qindesign::network::EthernetUDP*
  bool multicast = false;
  ~SocketState();
};

class Runtime
{
public:
  static Runtime& instance();

  // Registered objects are polled until only the registry references
  // them, then swept.
  std::shared_ptr<TimerState> makeTimer();
  std::shared_ptr<SocketState> openSocket(
    const ::ableton::discovery::IpAddress& addr, bool multicast);

  void post(std::function<void()> job);

  // Fire due timers, deliver received packets, drain posted jobs.
  void poll();

  // Send through a socket's UDP object. Returns bytes sent.
  static std::size_t send(SocketState& s, const uint8_t* data, std::size_t len,
    const ::ableton::discovery::UdpEndpoint& to);

private:
  Runtime() = default;

  std::vector<std::shared_ptr<TimerState>> mTimers;
  std::vector<std::shared_ptr<SocketState>> mSockets;
  std::vector<std::function<void()>> mQueue;
};

} // namespace teensy41
} // namespace platforms
} // namespace ableton

// Pump hook, callable from anywhere (also used by the condition_variable
// shim so Controller::shutdown() cannot deadlock).
extern "C" void neon_t41_link_poll();
