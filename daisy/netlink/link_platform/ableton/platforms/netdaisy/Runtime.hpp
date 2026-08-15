// The polled io runtime behind the Daisy netlink Link platform: one
// global registry of timers, UDP sockets, and posted jobs, pumped from
// the main loop by neon_netlink_link_poll(). Single-threaded by design —
// every Link call and every handler runs on the main thread, which is
// what lets the std::mutex shim be a no-op. The lwIP-facing half lives
// in link_runtime_netlink.cpp so this header stays includable without
// lwIP.

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include <ableton/discovery/AsioTypes.hpp>

int64_t daisy_now_us();

namespace ableton
{
namespace platforms
{
namespace netdaisy
{

struct TimerState
{
  int64_t dueUs = 0;
  bool armed = false;
  std::function<void(::neonnet::error_code)> handler;
};

// One received datagram, queued by the lwIP udp_recv callback (which
// fires inside the netif input pump — same thread) and delivered to the
// one-shot handler from Runtime::poll().
struct Datagram
{
  ::ableton::discovery::UdpEndpoint from;
  std::vector<uint8_t> data;
};

struct SocketState
{
  ::ableton::discovery::UdpEndpoint local;
  std::function<void(const ::ableton::discovery::UdpEndpoint&, const uint8_t*,
    const uint8_t*)>
    handler;
  void* pcb = nullptr; // lwIP udp_pcb*
  bool multicast = false;
  std::vector<Datagram> rxQueue;
  ~SocketState();
};

class Runtime
{
public:
  static Runtime& instance();

  std::shared_ptr<TimerState> makeTimer();
  std::shared_ptr<SocketState> openSocket(
    const ::ableton::discovery::IpAddress& addr, bool multicast);

  void post(std::function<void()> job);

  // Fire due timers, deliver received packets, drain posted jobs.
  void poll();

  // Send through a socket's pcb. Returns bytes sent.
  static std::size_t send(SocketState& s, const uint8_t* data, std::size_t len,
    const ::ableton::discovery::UdpEndpoint& to);

private:
  Runtime() = default;

  std::vector<std::shared_ptr<TimerState>> mTimers;
  std::vector<std::shared_ptr<SocketState>> mSockets;
  std::vector<std::function<void()>> mQueue;
};

} // namespace netdaisy
} // namespace platforms
} // namespace ableton

// Pump hook, callable from anywhere (also used by the condition_variable
// shim so Controller::shutdown() cannot deadlock).
extern "C" void neon_netlink_link_poll();
