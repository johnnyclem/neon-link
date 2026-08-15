// lwIP-facing half of the Daisy netlink Link platform runtime (see
// ableton/platforms/netdaisy/Runtime.hpp for the polled model). The
// teensy41 runtime with QNEthernet's EthernetUDP swapped for raw
// udp_pcbs: receive callbacks (fired inside usbnet::poll's input pump,
// same thread) queue datagrams on the socket, and poll() hands them to
// the one-shot handlers Link arms.

#include <ableton/platforms/netdaisy/Runtime.hpp>

#include "lwip/igmp.h"
#include "lwip/ip_addr.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"

#include <cstring>

namespace ableton
{
namespace platforms
{
namespace netdaisy
{

namespace
{

// Discovery packets are small; 1600 covers a full MTU with headroom.
constexpr std::size_t kRxBuf = 1600;
// Bursts on session join outrun a single-packet buffer; match the
// teensy runtime's 8-packet socket queue.
constexpr std::size_t kRxQueueCap = 8;

// Link's IPv4 discovery group.
const ip4_addr_t kLinkGroup = IPADDR4_INIT_BYTES(224, 76, 78, 75);
constexpr uint16_t kLinkPort = 20808;

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

udp_pcb* pcb_of(SocketState& s)
{
  return static_cast<udp_pcb*>(s.pcb);
}

void on_udp_recv(
  void* arg, struct udp_pcb* pcb, struct pbuf* p, const ip_addr_t* addr, u16_t port)
{
  (void)pcb;
  auto* s = static_cast<SocketState*>(arg);
  if (p == nullptr)
  {
    return;
  }
  if (s != nullptr && p->tot_len <= kRxBuf && s->rxQueue.size() < kRxQueueCap
      && IP_IS_V4(addr))
  {
    Datagram d;
    d.from = discovery::UdpEndpoint{
      discovery::IpAddressV4{lwip_ntohl(ip4_addr_get_u32(ip_2_ip4(addr)))},
      port};
    d.data.resize(p->tot_len);
    pbuf_copy_partial(p, d.data.data(), p->tot_len, 0);
    s->rxQueue.push_back(std::move(d));
  }
  pbuf_free(p);
}

} // namespace

SocketState::~SocketState()
{
  if (pcb != nullptr)
  {
    udp_remove(static_cast<udp_pcb*>(pcb));
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
  udp_pcb* pcb = udp_new_ip_type(IPADDR_TYPE_V4);
  s->pcb = pcb;
  s->multicast = multicast;
  if (pcb == nullptr)
  {
    return s;
  }
  udp_recv(pcb, on_udp_recv, s.get());
  if (multicast)
  {
    // Bind the well-known port and join the discovery group on every
    // IGMP-capable interface (there is exactly one).
    (void)udp_bind(pcb, IP_ANY_TYPE, kLinkPort);
    (void)igmp_joingroup(IP4_ADDR_ANY4, &kLinkGroup);
    s->local = discovery::UdpEndpoint{addr, kLinkPort};
  }
  else
  {
    const uint16_t port = next_ephemeral_port();
    (void)udp_bind(pcb, IP_ANY_TYPE, port);
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
  udp_pcb* pcb = pcb_of(s);
  if (pcb == nullptr || !to.address().is_v4())
  {
    return 0;
  }
  struct pbuf* p = pbuf_alloc(PBUF_TRANSPORT, (u16_t)len, PBUF_RAM);
  if (p == nullptr)
  {
    return 0;
  }
  std::memcpy(p->payload, data, len);
  ip_addr_t dst;
  ip_addr_set_ip4_u32_val(dst, lwip_htonl(to.address().to_v4().to_uint()));
  const err_t err = udp_sendto(pcb, p, &dst, to.port());
  pbuf_free(p);
  return err == ERR_OK ? len : 0;
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

  const int64_t now = daisy_now_us();
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
    // Drain everything queued this pass; each armed handler is one-shot
    // and re-armed inside the callback (the Link pattern).
    for (int guard = 0; guard < 8; ++guard)
    {
      if (!s->handler || s->rxQueue.empty())
      {
        break;
      }
      Datagram d = std::move(s->rxQueue.front());
      s->rxQueue.erase(s->rxQueue.begin());
      auto h = std::move(s->handler);
      s->handler = nullptr;
      h(d.from, d.data.data(), d.data.data() + d.data.size());
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

} // namespace netdaisy
} // namespace platforms
} // namespace ableton

extern "C" void neon_netlink_link_poll()
{
  ableton::platforms::netdaisy::Runtime::instance().poll();
}

// The condition_variable shim's pump (weak there, strong here): keeps
// Controller::shutdown()'s wait making progress on the single thread.
extern "C" void neon_netlink_cv_pump()
{
  neon_netlink_link_poll();
}

// link_service_daisy.cpp's weak per-tick hook (session_daisy.h): the
// netlink build pumps the Link runtime from the service loop.
extern "C" void neon_daisy_link_pump()
{
  neon_netlink_link_poll();
}
