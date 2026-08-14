#include "timebase_daisy.h"

#include "daisy_seed.h"
#include "irq_lock_daisy.h"

namespace {
uint32_t g_ticks_per_us = 200;  // corrected by daisy_time_init()
uint32_t g_last = 0;
uint64_t g_high = 0;
}  // namespace

void daisy_time_init() {
  const uint32_t freq = daisy::System::GetTickFreq();
  g_ticks_per_us = freq >= 1000000 ? freq / 1000000 : 1;
}

int64_t daisy_now_us() {
  const uint32_t primask = irq_save();
  const uint32_t now = daisy::System::GetTick();
  if (now < g_last) {
    g_high += 1ull << 32;
  }
  g_last = now;
  // Extend in ticks, divide once: no cumulative error from the non-integer
  // number of microseconds in a 32-bit tick wrap.
  const uint64_t ticks = g_high + now;
  irq_restore(primask);
  return static_cast<int64_t>(ticks / g_ticks_per_us);
}
