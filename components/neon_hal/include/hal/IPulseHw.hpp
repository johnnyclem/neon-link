#pragma once

#include <cstdint>

#include "hal/IClockSource.hpp"

namespace hal {

// One hardware edge: at time t_us, set then clear the given GPIO masks.
// Masks address GPIOs 0–31 (all pulse outputs are placed below GPIO 32 so
// a single set/clear register write covers them).
struct PulseEdge {
  int64_t t_us;
  uint32_t gpio_set_mask;
  uint32_t gpio_clear_mask;
};

// Emits pre-scheduled edges with hardware-timer precision. Implementations
// must accept edges in non-decreasing t_us order from a single producer.
class IPulseHw : public IClockSource {
 public:
  // Queue an edge for emission. Returns false if the queue is full (the
  // producer should back off and retry). t_us must be in this clock's
  // now_us() domain and comfortably in the future (>= ~2 ms lead).
  virtual bool submit(const PulseEdge& e) = 0;
};

}  // namespace hal
