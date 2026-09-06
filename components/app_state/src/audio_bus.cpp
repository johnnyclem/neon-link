#include "app_state/audio_bus.h"

#include <atomic>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

namespace {

QueueHandle_t synth_queue() {
  static QueueHandle_t q = xQueueCreate(32, sizeof(SynthEvent));
  return q;
}

QueueHandle_t onset_queue() {
  static QueueHandle_t q = xQueueCreate(16, sizeof(neon::OnsetEvent));
  return q;
}

std::atomic<bool> g_follow_no_adc{false};

}  // namespace

neon::SeqLock<neon::AudioEngineConfig>& audio_config_bus() {
  static neon::SeqLock<neon::AudioEngineConfig> bus;
  return bus;
}

neon::SeqLock<neon::AudioStatus>& audio_status_bus() {
  static neon::SeqLock<neon::AudioStatus> bus;
  return bus;
}

neon::SeqLock<neon::FollowStatus>& follow_status_bus() {
  static neon::SeqLock<neon::FollowStatus> bus;
  return bus;
}

bool synth_queue_push(const SynthEvent& ev) {
  return xQueueSend(synth_queue(), &ev, 0) == pdTRUE;
}

bool synth_queue_pop(SynthEvent* ev) {
  return xQueueReceive(synth_queue(), ev, 0) == pdTRUE;
}

bool onset_queue_push(const neon::OnsetEvent& ev) {
  return xQueueSend(onset_queue(), &ev, 0) == pdTRUE;
}

bool onset_queue_pop(neon::OnsetEvent* ev) {
  return xQueueReceive(onset_queue(), ev, 0) == pdTRUE;
}

void follow_set_no_adc(bool v) {
  g_follow_no_adc.store(v, std::memory_order_relaxed);
}

bool follow_no_adc() {
  return g_follow_no_adc.load(std::memory_order_relaxed);
}
