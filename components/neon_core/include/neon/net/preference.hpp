#pragma once

#include <cstdint>

namespace neon {

enum class ActiveNet : uint8_t { kNone, kEthernet, kWifi };

// Interface preference policy (SOFTWARE.md §5: prefer Ethernet when a
// cable is present, fall back to WiFi, enter AP mode for initial setup).
// Pure logic — the ESP-side net_manager feeds it link/IP events and acts
// on its outputs; host tests drive it with synthetic sequences.
//
// AP recommendation: nothing is connected AND either WiFi was never
// configured (short grace) or the configured networks have been down for
// an extended period (long grace). The grace timer restarts whenever any
// connectivity returns.
class NetPreference {
 public:
  static constexpr int64_t kApGraceUnconfiguredUs = 10 * 1000000ll;
  static constexpr int64_t kApGraceConfiguredUs = 60 * 1000000ll;

  void eth_link(bool up) {
    eth_link_ = up;
    if (!up) {
      eth_ip_ = false;
    }
  }
  void eth_ip(bool has) { eth_ip_ = has && eth_link_; }
  void wifi_configured(bool configured) { wifi_configured_ = configured; }
  void wifi_ip(bool has) { wifi_ip_ = has; }

  // Ethernet wins whenever it has an address (lower jitter, more
  // reliable); WiFi is the fallback.
  ActiveNet active() const {
    if (eth_ip_) {
      return ActiveNet::kEthernet;
    }
    if (wifi_ip_) {
      return ActiveNet::kWifi;
    }
    return ActiveNet::kNone;
  }

  // Call periodically with a monotonic clock; returns true when the setup
  // access point should be brought up.
  bool update_should_start_ap(int64_t now_us) {
    if (active() != ActiveNet::kNone) {
      all_down_since_us_ = -1;
      return false;
    }
    if (all_down_since_us_ < 0) {
      all_down_since_us_ = now_us;
    }
    const int64_t grace =
        wifi_configured_ ? kApGraceConfiguredUs : kApGraceUnconfiguredUs;
    return now_us - all_down_since_us_ >= grace;
  }

 private:
  bool eth_link_ = false;
  bool eth_ip_ = false;
  bool wifi_ip_ = false;
  bool wifi_configured_ = false;
  int64_t all_down_since_us_ = -1;
};

}  // namespace neon
