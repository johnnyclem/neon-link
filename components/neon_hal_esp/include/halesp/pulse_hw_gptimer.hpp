#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "driver/gptimer.h"
#include "hal/IPulseHw.hpp"

namespace halesp {

// ISR self-instrumentation: lateness between an edge's scheduled time and
// its actual emission (interrupt latency + queue effects). Exposed on
// /api/status so first hardware bring-up quantifies jitter for free.
struct PulseStats {
  uint32_t edges;
  uint32_t late_max_us;
  uint32_t late_avg_us;
};
PulseStats pulse_stats();

// GPTimer-based pulse emitter (see docs/ARCHITECTURE.md for the RMT vs
// GPTimer decision). One 1 MHz timer; the alarm is always armed at the
// earliest pending edge, and the IRAM ISR writes GPIO set/clear registers
// directly, then re-arms for the next edge (or parks with a 1 ms poll when
// the queue is empty — the producer keeps a >= 2 ms lead so a parked poll
// can never miss an edge).
//
// Single-producer (core 1 pulse task) / single-consumer (ISR) ring buffer;
// edges must be submitted in non-decreasing time order.
class PulseHwGptimer final : public hal::IPulseHw {
 public:
  // gpios: output pins to claim (all must be < 32). Returns false on any
  // driver error.
  bool init(const int* gpios, size_t count);

  bool submit(const hal::PulseEdge& e) override;
  int64_t now_us() const override;

 private:
  static bool on_alarm(gptimer_handle_t timer,
                       const gptimer_alarm_event_data_t* edata, void* user);

  static constexpr size_t kRingSize = 256;  // power of two

  gptimer_handle_t timer_ = nullptr;
  // esp_timer domain minus gptimer count domain, fixed at init. Both derive
  // from the same crystal, so the offset is constant (no drift).
  int64_t offset_us_ = 0;

  hal::PulseEdge ring_[kRingSize] = {};
  std::atomic<uint32_t> head_{0};  // producer writes
  std::atomic<uint32_t> tail_{0};  // ISR writes
};

}  // namespace halesp
