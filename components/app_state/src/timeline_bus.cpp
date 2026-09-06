#include "app_state/timeline_bus.h"

#include <atomic>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

namespace {
std::atomic<uint32_t> g_peers{0};
std::atomic<bool> g_ext_clock{false};
std::atomic<uint8_t> g_follow_source{0};
std::atomic<bool> g_playing{false};

QueueHandle_t gate_queue() {
  static QueueHandle_t q = xQueueCreate(16, sizeof(GateEvent));
  return q;
}

QueueHandle_t control_queue() {
  static QueueHandle_t q = xQueueCreate(16, sizeof(ControlCommand));
  return q;
}

QueueHandle_t midi_sync_queue() {
  static QueueHandle_t q = xQueueCreate(64, sizeof(neon::midi::SyncEvent));
  return q;
}
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

void app_status_set_follow_source(FollowSource source) {
  g_follow_source.store(static_cast<uint8_t>(source),
                        std::memory_order_relaxed);
}
FollowSource app_status_follow_source() {
  return static_cast<FollowSource>(
      g_follow_source.load(std::memory_order_relaxed));
}

bool gate_queue_push(const GateEvent& ev) {
  return xQueueSend(gate_queue(), &ev, 0) == pdTRUE;
}

bool gate_queue_pop(GateEvent* ev) {
  return xQueueReceive(gate_queue(), ev, 0) == pdTRUE;
}

bool control_queue_push(const ControlCommand& cmd) {
  return xQueueSend(control_queue(), &cmd, 0) == pdTRUE;
}

bool control_queue_pop(ControlCommand* cmd) {
  return xQueueReceive(control_queue(), cmd, 0) == pdTRUE;
}

bool midi_sync_queue_push(const neon::midi::SyncEvent& ev) {
  return xQueueSend(midi_sync_queue(), &ev, 0) == pdTRUE;
}

bool midi_sync_queue_pop(neon::midi::SyncEvent* ev) {
  return xQueueReceive(midi_sync_queue(), ev, 0) == pdTRUE;
}

void app_status_set_transport(bool playing) {
  g_playing.store(playing, std::memory_order_relaxed);
}
bool app_status_transport() { return g_playing.load(std::memory_order_relaxed); }
