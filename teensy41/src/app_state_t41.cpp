// Teensy implementation of the shared app_state buses
// (components/app_state/include/app_state/*.h — the same headers the
// ESP32 firmware compiles against). The seqlock types are portable;
// the FreeRTOS queues become interrupt-masked rings, sized like their
// ESP counterparts.

#include "app_state/audio_bus.h"
#include "app_state/timeline_bus.h"

#include <atomic>

#include "irq_lock_t41.h"

namespace {

template <typename T, size_t N>
class IrqRing {
 public:
  bool push(const T& v) {
    const uint32_t primask = irq_save();
    const uint32_t next = (head_ + 1) % N;
    if (next == tail_) {
      irq_restore(primask);
      return false;
    }
    buf_[head_] = v;
    head_ = next;
    irq_restore(primask);
    return true;
  }
  bool pop(T* out) {
    const uint32_t primask = irq_save();
    if (tail_ == head_) {
      irq_restore(primask);
      return false;
    }
    *out = buf_[tail_];
    tail_ = (tail_ + 1) % N;
    irq_restore(primask);
    return true;
  }

 private:
  T buf_[N] = {};
  volatile uint32_t head_ = 0;
  volatile uint32_t tail_ = 0;
};

neon::SeqLock<neon::TimelineSnapshot> g_timeline;
neon::SeqLock<neon::EngineConfig> g_engine_config;
neon::SeqLock<neon::AudioEngineConfig> g_audio_config;
neon::SeqLock<neon::AudioStatus> g_audio_status;
neon::SeqLock<neon::FollowStatus> g_follow_status;

IrqRing<GateEvent, 32> g_gates;
IrqRing<ControlCommand, 16> g_control;
IrqRing<SynthEvent, 32> g_synth;
IrqRing<neon::OnsetEvent, 16> g_onsets;

std::atomic<uint32_t> g_peers{0};
std::atomic<bool> g_ext_clock{false};
std::atomic<bool> g_transport{false};
std::atomic<bool> g_follow_no_adc{false};

}  // namespace

neon::SeqLock<neon::TimelineSnapshot>& timeline_bus() { return g_timeline; }
neon::SeqLock<neon::EngineConfig>& engine_config_bus() {
  return g_engine_config;
}
neon::SeqLock<neon::AudioEngineConfig>& audio_config_bus() {
  return g_audio_config;
}
neon::SeqLock<neon::AudioStatus>& audio_status_bus() { return g_audio_status; }
neon::SeqLock<neon::FollowStatus>& follow_status_bus() {
  return g_follow_status;
}

void app_status_set_peers(uint32_t peers) { g_peers = peers; }
uint32_t app_status_peers() { return g_peers; }
void app_status_set_ext_clock(bool active) { g_ext_clock = active; }
bool app_status_ext_clock() { return g_ext_clock; }
void app_status_set_transport(bool playing) { g_transport = playing; }
bool app_status_transport() { return g_transport; }

bool gate_queue_push(const GateEvent& ev) { return g_gates.push(ev); }
bool gate_queue_pop(GateEvent* ev) { return g_gates.pop(ev); }
bool control_queue_push(const ControlCommand& cmd) {
  return g_control.push(cmd);
}
bool control_queue_pop(ControlCommand* cmd) { return g_control.pop(cmd); }
bool synth_queue_push(const SynthEvent& ev) { return g_synth.push(ev); }
bool synth_queue_pop(SynthEvent* ev) { return g_synth.pop(ev); }
bool onset_queue_push(const neon::OnsetEvent& ev) { return g_onsets.push(ev); }
bool onset_queue_pop(neon::OnsetEvent* ev) { return g_onsets.pop(ev); }
void follow_set_no_adc(bool v) { g_follow_no_adc = v; }
bool follow_no_adc() { return g_follow_no_adc; }
