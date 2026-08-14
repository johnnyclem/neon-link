#include "pulse_hw_t41.h"

#include <Arduino.h>

#include "board_pins_t41.h"
#include "timebase_t41.h"

namespace {

constexpr uint32_t kTickUs = 100;
constexpr size_t kRingSize = 256;  // power of two
static_assert((kRingSize & (kRingSize - 1)) == 0, "ring must be 2^n");

hal::PulseEdge g_ring[kRingSize];
volatile uint32_t g_head = 0;  // consumer (ISR)
volatile uint32_t g_tail = 0;  // producer (loop)

volatile uint32_t g_levels = 0;
volatile uint32_t g_late = 0;
volatile uint32_t g_edges = 0;
volatile uint32_t g_late_max_us = 0;
volatile uint64_t g_late_sum_us = 0;

IntervalTimer g_timer;

inline void apply_masks(uint32_t set_mask, uint32_t clear_mask) {
  for (int i = 0; i < 6; ++i) {
    const uint32_t bit = 1u << i;
    if (set_mask & bit) {
      digitalWriteFast(kPulsePins[i], HIGH);
      g_levels |= bit;
    }
    if (clear_mask & bit) {
      digitalWriteFast(kPulsePins[i], LOW);
      g_levels &= ~bit;
    }
  }
}

void pulse_isr() {
  const int64_t now = t41_now_us();
  while (g_head != g_tail) {
    const hal::PulseEdge& e = g_ring[g_head & (kRingSize - 1)];
    if (e.t_us > now) {
      break;
    }
    const int64_t late = now - e.t_us;
    if (late > static_cast<int64_t>(kTickUs)) {
      g_late = g_late + 1;
    }
    if (late > 0) {
      g_late_sum_us = g_late_sum_us + static_cast<uint64_t>(late);
      if (static_cast<uint32_t>(late) > g_late_max_us) {
        g_late_max_us = static_cast<uint32_t>(late);
      }
    }
    g_edges = g_edges + 1;
    apply_masks(e.gpio_set_mask, e.gpio_clear_mask);
    g_head = g_head + 1;
  }
}

}  // namespace

bool PulseHwT41::init() {
  for (int pin : kPulsePins) {
    pinMode(pin, OUTPUT);
    digitalWriteFast(pin, LOW);
  }
  // Above USB/systick housekeeping, below nothing that matters more.
  g_timer.priority(48);
  return g_timer.begin(pulse_isr, kTickUs);
}

bool PulseHwT41::submit(const hal::PulseEdge& e) {
  if (g_tail - g_head >= kRingSize) {
    return false;
  }
  g_ring[g_tail & (kRingSize - 1)] = e;
  // Publish after the payload is in place. The ISR only reads entries
  // strictly below g_tail, and the ARMv7-M single-core memory model keeps
  // program order visible to the interrupt handler.
  asm volatile("dmb" ::: "memory");
  g_tail = g_tail + 1;
  return true;
}

int64_t PulseHwT41::now_us() const { return t41_now_us(); }

uint32_t PulseHwT41::levels() { return g_levels; }

uint32_t PulseHwT41::edges() { return g_edges; }

uint32_t PulseHwT41::late_max_us() { return g_late_max_us; }

uint32_t PulseHwT41::late_avg_us() {
  const uint32_t n = g_edges;
  return n != 0 ? static_cast<uint32_t>(g_late_sum_us / n) : 0;
}

uint32_t PulseHwT41::late_edges() { return g_late; }
