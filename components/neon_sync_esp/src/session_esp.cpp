// Neon Sync on ESP-IDF: sockets, task, and radio glue around the portable
// nsync::Node. The node itself is sans-I/O and host-tested; everything in
// this file is plumbing:
//
//  - one UDP socket bound to *:20809, joined to 239.77.83.78 (rejoined
//    periodically so interface changes — STA associating after boot, the
//    cable appearing — don't strand the membership),
//  - a core-0 task that pumps rx into the node and lets the node emit,
//  - TSF sampling (esp_wifi_get_tsf_time) for the same-BSS fast path;
//    on targets/paths where TSF is unavailable the call fails cleanly and
//    peers use the measured path.
//
// Threading: the node is guarded by one mutex. The service task (10 ms
// tick, capture/setters) and this socket task are the only callers.

#include "sdkconfig.h"

#include <cstring>

#include "lwip/sockets.h"

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "nsync/node.hpp"
#include "nsyncesp/session.hpp"

namespace nsyncesp {
namespace {

const char* kTag = "nsync";

constexpr int64_t kTsfSampleUs = 500000;
constexpr int64_t kRejoinUs = 10 * 1000000ll;

uint64_t node_id_from_mac() {
  uint8_t mac[6] = {};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  uint64_t id = 0;
  for (int i = 0; i < 6; ++i) {
    id = (id << 8) | mac[i];
  }
  // Keep a nonzero high byte so the id can never collapse to 0.
  return id | (0x4Eull << 56);  // 'N'
}

class NeonSyncSession final : public hal::ILinkSession {
 public:
  void start(double initial_bpm) override {
    ensure_mutex();
    {
      Lock l(mutex_);
      if (node_ == nullptr) {
        nsync::NodeConfig cfg;
        cfg.node_id = node_id_from_mac();
        node_ = new nsync::Node(cfg);
      }
      node_->start(initial_bpm, esp_timer_get_time());
    }
    if (task_ == nullptr) {
      xTaskCreatePinnedToCore(task_trampoline, "nsync", 6144, this, 9,
                              &task_, 0);
      ESP_LOGI(kTag, "Neon Sync session up (port %u, group %s)",
               static_cast<unsigned>(nsync::kPort), nsync::kMulticastGroup);
    }
  }

  bool capture(hal::LinkState& out) override {
    ensure_mutex();
    Lock l(mutex_);
    if (node_ == nullptr) {
      return false;
    }
    return node_->capture(out, esp_timer_get_time());
  }

  void set_tempo(double bpm) override {
    with_node([&](nsync::Node& n, int64_t now) { n.set_tempo(bpm, now); });
  }

  void set_playing(bool playing) override {
    with_node(
        [&](nsync::Node& n, int64_t now) { n.set_playing(playing, now); });
  }

  void request_beat_at_time(int64_t t_us) override {
    with_node(
        [&](nsync::Node& n, int64_t now) { n.request_beat_at_time(t_us, now); });
  }

  void set_start_stop_sync(bool enable) override {
    with_node([&](nsync::Node& n, int64_t now) {
      n.set_start_stop_sync(enable, now);
    });
  }

  void set_quantum(double beats) override {
    ensure_mutex();
    Lock l(mutex_);
    if (node_ == nullptr) {
      // Before start(): construct the node so the setting is not lost.
      nsync::NodeConfig cfg;
      cfg.node_id = node_id_from_mac();
      node_ = new nsync::Node(cfg);
    }
    node_->set_quantum(beats, esp_timer_get_time());
  }

 private:
  struct Lock {
    explicit Lock(SemaphoreHandle_t m) : m_(m) {
      xSemaphoreTake(m_, portMAX_DELAY);
    }
    ~Lock() { xSemaphoreGive(m_); }
    SemaphoreHandle_t m_;
  };

  // Sends straight onto the socket; dest 0 is the multicast group.
  class SocketEmitter : public nsync::Emitter {
   public:
    SocketEmitter(int sock, uint32_t group_be) : sock_(sock), group_be_(group_be) {}
    void send(nsync::Addr dest, const uint8_t* data, size_t len) override {
      if (sock_ < 0) {
        return;
      }
      sockaddr_in to{};
      to.sin_family = AF_INET;
      to.sin_port = htons(nsync::kPort);
      to.sin_addr.s_addr = dest == kMulticast ? group_be_ : dest;
      if (::sendto(sock_, data, len, 0,
                   reinterpret_cast<sockaddr*>(&to), sizeof(to)) < 0) {
        failed = true;
      }
    }
    bool failed = false;

   private:
    int sock_;
    uint32_t group_be_;
  };

  template <typename F>
  void with_node(F&& f) {
    ensure_mutex();
    Lock l(mutex_);
    if (node_ != nullptr) {
      f(*node_, esp_timer_get_time());
    }
  }

  void ensure_mutex() {
    if (mutex_ == nullptr) {
      mutex_ = xSemaphoreCreateMutex();
    }
  }

  static void task_trampoline(void* arg) {
    static_cast<NeonSyncSession*>(arg)->task_loop();
  }

  bool open_socket() {
    sock_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock_ < 0) {
      return false;
    }
    int yes = 1;
    ::setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
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
    uint8_t loop = 0;  // the node drops own-id packets anyway
    ::setsockopt(sock_, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));
    join_group();
    return true;
  }

  void join_group() {
    // Re-issued periodically: cheap, and it re-registers the membership
    // after netif churn (STA association, Ethernet link-up). lwIP returns
    // an error for an already-joined group; that is fine.
    ip_mreq mreq{};
    mreq.imr_multiaddr.s_addr = group_be_;
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    ::setsockopt(sock_, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq));
    if (::setsockopt(sock_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq,
                     sizeof(mreq)) == 0 &&
        !joined_logged_) {
      ESP_LOGI(kTag, "joined %s", nsync::kMulticastGroup);
      joined_logged_ = true;
    }
    last_join_us_ = esp_timer_get_time();
  }

  void close_socket() {
    if (sock_ >= 0) {
      ::close(sock_);
      sock_ = -1;
    }
  }

  void sample_tsf() {
    // Same-BSS fast path: hand the node a (bssid, tsf, local) pair when
    // the STA is associated and the driver exposes TSF. Every failure
    // path just leaves the node on the measured estimator.
    wifi_ap_record_t ap{};
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) {
      return;
    }
    const int64_t tsf = esp_wifi_get_tsf_time(WIFI_IF_STA);
    const int64_t local = esp_timer_get_time();
    if (tsf <= 0) {
      return;
    }
    Lock l(mutex_);
    if (node_ != nullptr) {
      node_->set_local_tsf(ap.bssid, static_cast<uint64_t>(tsf), local);
    }
  }

  void task_loop() {
    group_be_ = inet_addr(nsync::kMulticastGroup);
    int64_t next_tsf_us = 0;
    for (;;) {
      if (sock_ < 0 && !open_socket()) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        continue;
      }

      fd_set rfds;
      FD_ZERO(&rfds);
      FD_SET(sock_, &rfds);
      timeval tv{};
      tv.tv_usec = 20000;
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
          Lock l(mutex_);
          if (node_ != nullptr) {
            node_->handle_packet(buf, static_cast<size_t>(n),
                                 from.sin_addr.s_addr, esp_timer_get_time(),
                                 tx);
          }
          from_len = sizeof(from);
        }
      } else if (ready < 0) {
        close_socket();
        continue;
      }

      const int64_t now = esp_timer_get_time();
      if (now >= next_tsf_us) {
        sample_tsf();
        next_tsf_us = now + kTsfSampleUs;
      }
      {
        Lock l(mutex_);
        if (node_ != nullptr) {
          node_->poll(esp_timer_get_time(), tx);
        }
      }
      if (tx.failed || now - last_join_us_ > kRejoinUs) {
        // Send errors usually mean the interface moved under us; a
        // rejoin (or a fresh socket) repairs both directions.
        if (tx.failed) {
          close_socket();
        } else {
          join_group();
        }
      }
    }
  }

  nsync::Node* node_ = nullptr;
  SemaphoreHandle_t mutex_ = nullptr;
  TaskHandle_t task_ = nullptr;
  int sock_ = -1;
  uint32_t group_be_ = 0;
  int64_t last_join_us_ = 0;
  bool joined_logged_ = false;
};

NeonSyncSession g_session;

}  // namespace

hal::ILinkSession& session() { return g_session; }

}  // namespace nsyncesp
