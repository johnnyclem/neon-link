#pragma once

#include <cstdint>

#include "daisy_seed.h"

// CLK IN / RST IN capture. libDaisy has no EXTI wrapper, so edges are
// detected by the 10 kHz input sampler (the pulse-timer ISR): rising
// edges are timestamped to ±100 µs, well inside the ExtClockEstimator's
// median window and its 1 ms lockout. Events land in a small ring the
// link service drains.
namespace clkin {

enum class Kind : uint8_t { kClock = 0, kReset = 1 };

struct Event {
  Kind kind;
  int64_t t_us;
};

void init(daisy::Pin clk_pin, daisy::Pin rst_pin);

// Called from the 10 kHz sampling ISR.
void sample_isr(int64_t now_us);

// Main-loop side: drain captured events.
bool pop(Event* out);

}  // namespace clkin
