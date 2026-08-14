#include "clkin_t41.h"

#include <Arduino.h>

#include "irq_lock_t41.h"
#include "timebase_t41.h"

namespace clkin {
namespace {

constexpr int64_t kLockoutUs = 1000;
constexpr uint32_t kRingSize = 32;

Event g_ring[kRingSize];
volatile uint32_t g_head = 0;
volatile uint32_t g_tail = 0;
int64_t g_last[2] = {INT64_MIN, INT64_MIN};

void capture(Kind kind) {
  const int64_t now = t41_now_us();
  const int idx = static_cast<int>(kind);
  if (now - g_last[idx] < kLockoutUs) {
    return;
  }
  g_last[idx] = now;
  const uint32_t next = (g_head + 1) % kRingSize;
  if (next == g_tail) {
    return;  // full: drop, the estimator recovers from gaps
  }
  g_ring[g_head] = Event{kind, now};
  g_head = next;
}

void clk_isr() { capture(Kind::kClock); }
void rst_isr() { capture(Kind::kReset); }

}  // namespace

void init(int clk_pin, int rst_pin) {
  pinMode(clk_pin, INPUT);
  pinMode(rst_pin, INPUT);
  attachInterrupt(digitalPinToInterrupt(clk_pin), clk_isr, RISING);
  attachInterrupt(digitalPinToInterrupt(rst_pin), rst_isr, RISING);
}

bool pop(Event* out) {
  const uint32_t primask = irq_save();
  if (g_tail == g_head) {
    irq_restore(primask);
    return false;
  }
  *out = g_ring[g_tail];
  g_tail = (g_tail + 1) % kRingSize;
  irq_restore(primask);
  return true;
}

}  // namespace clkin
