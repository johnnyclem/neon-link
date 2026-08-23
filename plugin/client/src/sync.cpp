#include "neon/client/sync.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <random>

namespace neon::client {
namespace {

constexpr int64_t kRejoinUs = 10 * 1000000ll;

// The firmware derives ids from the MAC with 'N' (0x4E) pinned in the high
// byte (components/neon_sync_esp). The plugin pins 'D' (0x44), strictly
// below every device id, so the laptop can never become the session's
// ghost reference — the mesh keeps a device's timebase and the plugin
// disciplines its own mapping toward it (docs/NEON_SYNC.md §4.2).
uint64_t random_node_id() {
  std::random_device rd;
  uint64_t e = (static_cast<uint64_t>(rd()) << 32) ^ rd();
  e ^= static_cast<uint64_t>(
      std::chrono::steady_clock::now().time_since_epoch().count());
  e &= 0x00FFFFFFFFFFFFFFull;
  if (e == 0) {
    e = 1;
  }
  return e | (0x44ull << 56);
}

nsync::NodeConfig node_config(uint64_t id) {
  nsync::NodeConfig cfg;
  cfg.node_id = id;
  return cfg;
}

// Sends straight onto the socket; dest 0 is the multicast group. A closed
// socket (or one that died mid-loop) turns every send into a no-op.
class SocketEmitter : public nsync::Emitter {
 public:
  SocketEmitter(int sock, uint32_t group_be)
      : sock_(sock), group_be_(group_be) {}
  void send(nsync::Addr dest, const uint8_t* data, size_t len) override {
    if (sock_ < 0) {
      return;
    }
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(nsync::kPort);
    to.sin_addr.s_addr = dest == kMulticast ? group_be_ : dest;
    if (::sendto(sock_, data, len, 0, reinterpret_cast<sockaddr*>(&to),
                 sizeof(to)) < 0) {
      failed = true;
    }
  }
  bool failed = false;

 private:
  int sock_;
  uint32_t group_be_;
};

}  // namespace

SyncService::SyncService()
    : node_id_(random_node_id()),
      node_(node_config(node_id_)),
      follower_(node_) {}

SyncService::~SyncService() { stop(); }

int64_t SyncService::now_us() {
  return std::chrono::duration_cast<std::chrono::microseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

void SyncService::start(double initial_bpm) {
  {
    std::lock_guard<std::mutex> l(mu_);
    if (!node_.started()) {
      node_.start(initial_bpm, now_us());
    }
  }
  if (thread_.joinable()) {
    return;
  }
  stop_flag_.store(false, std::memory_order_release);
  running_.store(true, std::memory_order_release);
  thread_ = std::thread([this] { loop(); });
}

void SyncService::stop() {
  if (!thread_.joinable()) {
    return;
  }
  stop_flag_.store(true, std::memory_order_release);
  thread_.join();
  thread_ = std::thread();
  running_.store(false, std::memory_order_release);
}

void SyncService::set_drive(bool enabled) {
  std::lock_guard<std::mutex> l(mu_);
  follower_.set_drive(enabled);
}

bool SyncService::drive() const {
  std::lock_guard<std::mutex> l(mu_);
  return follower_.drive();
}

SyncStatus SyncService::status() const {
  SyncStatus s;
  const int64_t now = now_us();
  {
    std::lock_guard<std::mutex> l(mu_);
    static_cast<nsync::FollowerStatus&>(s) = follower_.status(now);
  }
  s.running = running();
  s.node_id = node_id_;
  return s;
}

bool SyncService::open_socket() {
  sock_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
  if (sock_ < 0) {
    return false;
  }
  int yes = 1;
  ::setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#ifdef SO_REUSEPORT
  ::setsockopt(sock_, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
#endif
  sockaddr_in bind_addr{};
  bind_addr.sin_family = AF_INET;
  bind_addr.sin_port = htons(nsync::kPort);
  bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  if (::bind(sock_, reinterpret_cast<sockaddr*>(&bind_addr),
             sizeof(bind_addr)) < 0) {
    close_socket();
    return false;
  }
  uint8_t ttl = 1;  // one segment; Neon Sync does not route
  ::setsockopt(sock_, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
  // Loopback stays on (unlike the ESP glue): a second local peer — another
  // plugin instance, a simulator — should hear us; the node drops its own
  // id's packets anyway.
  join_group();
  return true;
}

void SyncService::join_group() {
  // Re-issued periodically: cheap, and it re-registers the membership when
  // the laptop hops networks (WiFi roam, VPN up/down, cable events).
  ip_mreq mreq{};
  mreq.imr_multiaddr.s_addr = group_be_;
  mreq.imr_interface.s_addr = htonl(INADDR_ANY);
  ::setsockopt(sock_, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq));
  ::setsockopt(sock_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));
  last_join_us_ = now_us();
}

void SyncService::close_socket() {
  if (sock_ >= 0) {
    ::close(sock_);
    sock_ = -1;
  }
}

void SyncService::loop() {
  group_be_ = ::inet_addr(nsync::kMulticastGroup);
  while (!stop_flag_.load(std::memory_order_acquire)) {
    if (sock_ < 0 && !open_socket()) {
      // No usable netif (or the port is held): retry in a second, but
      // keep the follower/poll ticking so state stays coherent.
      for (int i = 0; i < 50 && !stop_flag_.load(std::memory_order_acquire);
           ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
      }
      SocketEmitter tx(-1, group_be_);
      std::lock_guard<std::mutex> l(mu_);
      nsync::DawPlayhead ph;
      if (mailbox_.read(ph)) {
        follower_.update(ph, now_us());
      }
      node_.poll(now_us(), tx);
      continue;
    }

    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(sock_, &rfds);
    timeval tv{};
    tv.tv_usec = 10000;
    const int ready = ::select(sock_ + 1, &rfds, nullptr, nullptr, &tv);

    SocketEmitter tx(sock_, group_be_);
    if (ready > 0 && FD_ISSET(sock_, &rfds)) {
      uint8_t buf[nsync::kMaxPacket];
      sockaddr_in from{};
      socklen_t from_len = sizeof(from);
      int n;
      while ((n = ::recvfrom(sock_, buf, sizeof(buf), MSG_DONTWAIT,
                             reinterpret_cast<sockaddr*>(&from),
                             &from_len)) > 0) {
        std::lock_guard<std::mutex> l(mu_);
        node_.handle_packet(buf, static_cast<size_t>(n), from.sin_addr.s_addr,
                            now_us(), tx);
        from_len = sizeof(from);
      }
    } else if (ready < 0 && errno != EINTR) {
      close_socket();
      continue;
    }

    {
      std::lock_guard<std::mutex> l(mu_);
      nsync::DawPlayhead ph;
      if (mailbox_.read(ph)) {
        follower_.update(ph, now_us());
      }
      node_.poll(now_us(), tx);
    }

    if (tx.failed) {
      // Send errors usually mean the interface moved under us; a fresh
      // socket (and group membership) repairs both directions.
      close_socket();
    } else if (now_us() - last_join_us_ > kRejoinUs) {
      join_group();
    }
  }

  // Clean exit: BYE so peers drop us now instead of waiting out the TTL.
  {
    SocketEmitter tx(sock_, group_be_);
    std::lock_guard<std::mutex> l(mu_);
    node_.stop(tx);
  }
  close_socket();
}

}  // namespace neon::client
