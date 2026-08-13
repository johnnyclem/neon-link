#include "app_state/audio_bus.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

namespace {

QueueHandle_t synth_queue() {
  static QueueHandle_t q = xQueueCreate(32, sizeof(SynthEvent));
  return q;
}

}  // namespace

neon::SeqLock<neon::AudioEngineConfig>& audio_config_bus() {
  static neon::SeqLock<neon::AudioEngineConfig> bus;
  return bus;
}

neon::SeqLock<neon::AudioStatus>& audio_status_bus() {
  static neon::SeqLock<neon::AudioStatus> bus;
  return bus;
}

bool synth_queue_push(const SynthEvent& ev) {
  return xQueueSend(synth_queue(), &ev, 0) == pdTRUE;
}

bool synth_queue_pop(SynthEvent* ev) {
  return xQueueReceive(synth_queue(), ev, 0) == pdTRUE;
}
