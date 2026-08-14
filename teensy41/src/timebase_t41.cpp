#include "timebase_t41.h"

#include <Arduino.h>

#include "irq_lock_t41.h"

namespace {
uint32_t g_last = 0;
uint64_t g_high = 0;
}  // namespace

int64_t t41_now_us() {
  const uint32_t primask = irq_save();
  const uint32_t now = micros();
  if (now < g_last) {
    g_high += 1ull << 32;
  }
  g_last = now;
  const uint64_t r = g_high + now;
  irq_restore(primask);
  return static_cast<int64_t>(r);
}
