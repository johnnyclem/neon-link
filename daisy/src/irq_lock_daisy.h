#pragma once

#include <cstdint>

// PRIMASK save/restore critical section (same 20 lines as the Teensy
// build's irq_lock_t41.h — the Cortex-M7 is the Cortex-M7). Restoring
// instead of blindly re-enabling keeps these helpers safe inside ISRs.
static inline uint32_t irq_save() {
  uint32_t primask;
  asm volatile("mrs %0, primask" : "=r"(primask));
  asm volatile("cpsid i" ::: "memory");
  return primask;
}

static inline void irq_restore(uint32_t primask) {
  if ((primask & 1u) == 0u) {
    asm volatile("cpsie i" ::: "memory");
  }
}
