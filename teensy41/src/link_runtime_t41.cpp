// QNEthernet-facing half of the Teensy Link platform runtime (see
// ableton/platforms/teensy41/Runtime.hpp for the polled model).

#include <ableton/platforms/teensy41/Runtime.hpp>

#include <QNEthernet.h>

namespace ableton
{
namespace platforms
{
namespace teensy41
{

namespace
{

using qindesign::network::EthernetUDP;

// Discovery packets are small; 1600 covers a full MTU with headroom.
constexpr std::size_t kRxBuf = 1600;

// Ephemeral local ports for unicast sockets. Sequential from a fixed
// base is fine: uniqueness within one boot is all Link needs.
uint16_t next_ephemeral_port()
{
  static uint16_t port = 41000;
  if (port > 60000)
  {
    port = 41000;
  }
  return port++;
}

IPAddress to_arduino(const discovery::IpAddress& a)
{
  const auto b = a.to_v4().to_bytes();
  return IPAddress{b[0], b[1], b[2], b[3]};
}

discovery::IpAddress from_arduino(const IPAddress& ip)
{
  return discovery::IpAddressV4{
    discovery::IpAddressV4::bytes_type{ip[0], ip[1], ip[2], ip[3]}};
}

EthernetUDP* udp_of(SocketState& s)
{
  return static_cast<EthernetUDP*>(s.udp);
}

} // namespace

SocketState::~SocketState()
{
  if (udp != nullptr)
  {
    static_cast<EthernetUDP*>(udp)->stop();
    delete static_cast<EthernetUDP*>(udp);
  }
}

Runtime& Runtime::instance()
{
  static Runtime rt;
  return rt;
}

std::shared_ptr<TimerState> Runtime::makeTimer()
{
  auto t = std::make_shared<TimerState>();
  mTimers.push_back(t);
  return t;
}

std::shared_ptr<SocketState> Runtime::openSocket(
  const discovery::IpAddress& addr, bool multicast)
{
  auto s = std::make_shared<SocketState>();
  // Queue a few packets: discovery bursts on session join outrun a
  // single-packet buffer.
  auto* udp = new EthernetUDP(8);
  s->udp = udp;
  s->multicast = multicast;
  if (multicast)
  {
    // Link's IPv4 discovery group. beginMulticast joins the group and
    // binds the well-known port.
    udp->beginMulticast(IPAddress{224, 76, 78, 75}, 20808);
    s->local = discovery::UdpEndpoint{addr, 20808};
  }
  else
  {
    const uint16_t port = next_ephemeral_port();
    udp->begin(port);
    s->local = discovery::UdpEndpoint{addr, port};
  }
  mSockets.push_back(s);
  return s;
}

void Runtime::post(std::function<void()> job)
{
  mQueue.push_back(std::move(job));
}

std::size_t Runtime::send(SocketState& s, const uint8_t* data, std::size_t len,
  const discovery::UdpEndpoint& to)
{
  EthernetUDP* udp = udp_of(s);
  if (udp == nullptr || !to.address().is_v4())
  {
    return 0;
  }
  if (!udp->beginPacket(to_arduino(to.address()), to.port()))
  {
    return 0;
  }
  udp->write(data, len);
  return udp->endPacket() ? len : 0;
}

void Runtime::poll()
{
  // Posted jobs first (swap so jobs posted by jobs run next pass, which
  // is asio-post-like and prevents unbounded recursion).
  if (!mQueue.empty())
  {
    std::vector<std::function<void()>> jobs;
    jobs.swap(mQueue);
    for (auto& j : jobs)
    {
      j();
    }
  }

  const int64_t now = t41_now_us();
  // Index loop: handlers may create timers (reallocating the vector).
  for (std::size_t i = 0; i < mTimers.size(); ++i)
  {
    auto t = mTimers[i];
    if (t->armed && t->handler && now >= t->dueUs)
    {
      t->armed = false;
      auto h = std::move(t->handler);
      t->handler = nullptr;
      h(::neonnet::error_code{0});
    }
  }

  for (std::size_t i = 0; i < mSockets.size(); ++i)
  {
    auto s = mSockets[i];
    EthernetUDP* udp = udp_of(*s);
    if (udp == nullptr)
    {
      continue;
    }
    // Drain everything queued this pass; each armed handler is one-shot
    // and re-armed inside the callback (the Link pattern).
    for (int guard = 0; guard < 8; ++guard)
    {
      const int size = udp->parsePacket();
      if (size <= 0)
      {
        break;
      }
      if (!s->handler || static_cast<std::size_t>(size) > kRxBuf)
      {
        udp->flush();
        continue;
      }
      static uint8_t buf[kRxBuf];
      const int got = udp->read(buf, sizeof(buf));
      if (got <= 0)
      {
        continue;
      }
      const discovery::UdpEndpoint from{
        from_arduino(udp->remoteIP()), udp->remotePort()};
      auto h = std::move(s->handler);
      s->handler = nullptr;
      h(from, buf, buf + got);
    }
  }

  // Sweep entries only the registry still owns (their Timer/Socket
  // wrapper objects are gone).
  for (std::size_t i = 0; i < mTimers.size();)
  {
    if (mTimers[i].use_count() == 1)
    {
      mTimers[i] = mTimers.back();
      mTimers.pop_back();
    }
    else
    {
      ++i;
    }
  }
  for (std::size_t i = 0; i < mSockets.size();)
  {
    if (mSockets[i].use_count() == 1)
    {
      mSockets[i] = mSockets.back();
      mSockets.pop_back();
    }
    else
    {
      ++i;
    }
  }
}

} // namespace teensy41
} // namespace platforms
} // namespace ableton

extern "C" void neon_t41_link_poll()
{
  ableton::platforms::teensy41::Runtime::instance().poll();
}

// The condition_variable shim's pump (weak there, strong here): keeps
// Controller::shutdown()'s wait making progress on the single thread.
extern "C" void neon_t41_cv_pump()
{
  neon_t41_link_poll();
}
