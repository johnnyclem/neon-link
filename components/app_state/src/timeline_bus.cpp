#include "app_state/timeline_bus.h"

#include <atomic>

namespace {
std::atomic<uint32_t> g_peers{0};
std::atomic<bool> g_ext_clock{false};
}  // namespace

neon::SeqLock<neon::TimelineSnapshot>& timeline_bus() {
  static neon::SeqLock<neon::TimelineSnapshot> bus;
  return bus;
}

neon::SeqLock<neon::EngineConfig>& engine_config_bus() {
  static neon::SeqLock<neon::EngineConfig> bus;
  return bus;
}

void app_status_set_peers(uint32_t peers) {
  g_peers.store(peers, std::memory_order_relaxed);
}
uint32_t app_status_peers() { return g_peers.load(std::memory_order_relaxed); }

void app_status_set_ext_clock(bool active) {
  g_ext_clock.store(active, std::memory_order_relaxed);
}
bool app_status_ext_clock() {
  return g_ext_clock.load(std::memory_order_relaxed);
}
