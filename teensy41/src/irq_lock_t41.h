#pragma once

#include <cstdint>

// PRIMASK save/restore critical section. The Teensy core exposes
// __disable_irq()/__enable_irq() but not the CMSIS __get_PRIMASK
// intrinsic, so read it directly — restoring instead of blindly
// re-enabling keeps these helpers safe inside ISRs.
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
