// Neon Sync on ESP-IDF: sockets, task, and radio glue around the portable
// nsync::Node. The node itself is sans-I/O and host-tested; everything in
// this file is plumbing:
//
//  - one UDP socket bound to *:20809, joined to 239.77.83.78 (rejoined
//    periodically so interface changes — STA associating after boot, the
//    cable appearing — don't strand the membership),
//  - a core-0 task that pumps rx into the node and lets the node emit,
//  - TSF sampling (esp_wifi_get_tsf_time) for the same-BSS fast path;
//    STA associated → AP beacon TSF; SoftAP host (Nearby "no network
//    here") → our own AP TSF so clients on this BSS share the clock.
//    On targets/paths where TSF is unavailable the call fails cleanly
//    and peers use the measured path.
//
// Threading: the node is guarded by one mutex. The service task (10 ms
// tick, capture/setters) and this socket task are the only callers.
// Outgoing packets are copied under the mutex and sendto() runs after
// unlock so a unicore C3 does not stall capture() on the radio.

#include "sdkconfig.h"

#include <cstring>

#include "lwip/inet.h"
#include "lwip/sockets.h"

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "nsync/node.hpp"
#include "nsyncesp/session.hpp"

#include <new>

namespace nsyncesp {
namespace {

const char* kTag = "nsync";

constexpr int64_t kTsfSampleUs = 500000;
constexpr int64_t kRejoinUs = 10 * 1000000ll;
constexpr int kEmitCap = 20;

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
      construct_node();
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

  void set_playing(bool playing, int64_t at_us) override {
    with_node([&](nsync::Node& n, int64_t now) {
      n.set_playing(playing, at_us >= 0 ? at_us : now);
    });
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
    construct_node();
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

  // Buffers packets while the node mutex is held, then flushes after
  // unlock so capture() is not blocked on sendto (unicore C3).
  struct Outgoing {
    nsync::Addr dest = 0;
    uint16_t len = 0;
    uint8_t data[nsync::kMaxPacket] = {};
  };

  class BufferingEmitter : public nsync::Emitter {
   public:
    BufferingEmitter(Outgoing* slots, int cap, int* n, bool* overflow)
        : slots_(slots), cap_(cap), n_(n), overflow_(overflow) {}
    void send(nsync::Addr dest, const uint8_t* data, size_t len) override {
      if (data == nullptr || len == 0 || len > nsync::kMaxPacket) {
        return;
      }
      if (*n_ >= cap_) {
        *overflow_ = true;
        return;
      }
      Outgoing& o = slots_[(*n_)++];
      o.dest = dest;
      o.len = static_cast<uint16_t>(len);
      std::memcpy(o.data, data, len);
    }

   private:
    Outgoing* slots_;
    int cap_;
    int* n_;
    bool* overflow_;
  };

  bool flush_outgoing(int n) {
    bool failed = false;
    for (int i = 0; i < n; ++i) {
      if (!send_one(outgoing_[i].dest, outgoing_[i].data, outgoing_[i].len)) {
        failed = true;
      }
    }
    return failed;
  }

  bool send_one(nsync::Addr dest, const uint8_t* data, size_t len) {
    if (sock_ < 0) {
      return false;
    }
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(nsync::kPort);
    to.sin_addr.s_addr = dest == nsync::Emitter::kMulticast ? group_be_ : dest;
    return ::sendto(sock_, data, len, 0, reinterpret_cast<sockaddr*>(&to),
                    sizeof(to)) >= 0;
  }

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

  void construct_node() {
    if (node_ != nullptr) {
      return;
    }
    nsync::NodeConfig cfg;
    cfg.node_id = node_id_from_mac();
    node_ = new (node_store_) nsync::Node(cfg);
  }

  static void task_trampoline(void* arg) {
    static_cast<NeonSyncSession*>(arg)->task_loop();
  }

  // STA associated → default netif (infrastructure, Nearby default).
  // SoftAP-only / APSTA-without-STA → pin to the AP so lwIP does not
  // emit on a dormant STA. 192.168.4.1 is the ESP-IDF SoftAP fallback
  // if the netif is not up yet.
  uint32_t multicast_if_be() {
    wifi_ap_record_t ap{};
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
      return htonl(INADDR_ANY);
    }
    wifi_mode_t mode = WIFI_MODE_NULL;
    if (esp_wifi_get_mode(&mode) != ESP_OK) {
      return htonl(INADDR_ANY);
    }
    if (mode != WIFI_MODE_AP && mode != WIFI_MODE_APSTA) {
      return htonl(INADDR_ANY);
    }
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (netif != nullptr) {
      esp_netif_ip_info_t info{};
      if (esp_netif_get_ip_info(netif, &info) == ESP_OK && info.ip.addr != 0) {
        return info.ip.addr;
      }
    }
    return inet_addr("192.168.4.1");
  }

  void apply_multicast_if() {
    in_addr ifaddr{};
    ifaddr.s_addr = multicast_if_be();
    ::setsockopt(sock_, IPPROTO_IP, IP_MULTICAST_IF, &ifaddr, sizeof(ifaddr));
  }

  void drop_membership(uint32_t if_be) {
    ip_mreq mreq{};
    mreq.imr_multiaddr.s_addr = group_be_;
    mreq.imr_interface.s_addr = if_be;
    ::setsockopt(sock_, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq));
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
    // after netif churn (STA association, Ethernet link-up, AP→STA hop).
    // lwIP returns an error for an already-joined group; that is fine.
    apply_multicast_if();
    const uint32_t if_be = multicast_if_be();
    drop_membership(htonl(INADDR_ANY));
    drop_membership(inet_addr("192.168.4.1"));
    if (if_be != htonl(INADDR_ANY) && if_be != inet_addr("192.168.4.1")) {
      drop_membership(if_be);
    }
    ip_mreq mreq{};
    mreq.imr_multiaddr.s_addr = group_be_;
    mreq.imr_interface.s_addr = if_be;
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
    joined_logged_ = false;
  }

  void sample_tsf() {
    // Same-BSS fast path. STA associated → AP beacon TSF. SoftAP host
    // (Nearby "no network here") → our own AP TSF + AP MAC as bssid so
    // clients on this BSS share the clock.
    uint8_t bssid[6] = {};
    int64_t tsf = 0;
    wifi_ap_record_t ap{};
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
      tsf = esp_wifi_get_tsf_time(WIFI_IF_STA);
      std::memcpy(bssid, ap.bssid, 6);
    } else {
      wifi_mode_t mode = WIFI_MODE_NULL;
      if (esp_wifi_get_mode(&mode) != ESP_OK) {
        return;
      }
      if (mode != WIFI_MODE_AP && mode != WIFI_MODE_APSTA) {
        return;
      }
      tsf = esp_wifi_get_tsf_time(WIFI_IF_AP);
      if (esp_wifi_get_mac(WIFI_IF_AP, bssid) != ESP_OK) {
        return;
      }
    }
    const int64_t local = esp_timer_get_time();
    if (tsf <= 0) {
      return;
    }
    Lock l(mutex_);
    if (node_ != nullptr) {
      node_->set_local_tsf(bssid, static_cast<uint64_t>(tsf), local);
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

      bool send_failed = false;
      bool overflow = false;
      int nout = 0;
      if (ready > 0 && FD_ISSET(sock_, &rfds)) {
        uint8_t buf[nsync::kMaxPacket];
        sockaddr_in from{};
        socklen_t from_len = sizeof(from);
        int n;
        while ((n = ::recvfrom(sock_, buf, sizeof(buf), MSG_DONTWAIT,
                               reinterpret_cast<sockaddr*>(&from),
                               &from_len)) > 0) {
          {
            Lock l(mutex_);
            if (node_ != nullptr) {
              BufferingEmitter tx(outgoing_, kEmitCap, &nout, &overflow);
              node_->handle_packet(buf, static_cast<size_t>(n),
                                   from.sin_addr.s_addr, esp_timer_get_time(),
                                   tx);
            }
          }
          from_len = sizeof(from);
        }
      } else if (ready < 0) {
        close_socket();
        continue;
      }
      send_failed = flush_outgoing(nout);
      nout = 0;

      const int64_t now = esp_timer_get_time();
      if (now >= next_tsf_us) {
        sample_tsf();
        next_tsf_us = now + kTsfSampleUs;
      }
      {
        Lock l(mutex_);
        if (node_ != nullptr) {
          BufferingEmitter tx(outgoing_, kEmitCap, &nout, &overflow);
          node_->poll(esp_timer_get_time(), tx);
        }
      }
      send_failed = flush_outgoing(nout) || send_failed;
      if (overflow) {
        ESP_LOGW(kTag, "emit buffer full; a packet was dropped");
      }
      if (send_failed || now - last_join_us_ > kRejoinUs) {
        // Send errors usually mean the interface moved under us; a
        // rejoin (or a fresh socket) repairs both directions.
        if (send_failed) {
          close_socket();
        } else {
          join_group();
        }
      }
    }
  }

  nsync::Node* node_ = nullptr;
  alignas(nsync::Node) uint8_t node_store_[sizeof(nsync::Node)];
  Outgoing outgoing_[kEmitCap];
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
