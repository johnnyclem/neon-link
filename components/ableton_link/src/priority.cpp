#include "ablink/priority.hpp"

#include <atomic>

namespace ablink {

namespace {
// Defaults match the shipped fix (link_overrides/.../Context.hpp,
// link_audio_esp.cpp) in case nothing ever calls the setters — the stub
// build and host tests among them.
std::atomic<int> g_asio_priority{12};
std::atomic<int> g_pump_priority{9};
}  // namespace

void set_link_asio_priority(int priority) {
  g_asio_priority.store(priority, std::memory_order_relaxed);
}

int link_asio_priority() {
  return g_asio_priority.load(std::memory_order_relaxed);
}

void set_link_pump_priority(int priority) {
  g_pump_priority.store(priority, std::memory_order_relaxed);
}

int link_pump_priority() {
  return g_pump_priority.load(std::memory_order_relaxed);
}

}  // namespace ablink
