#include "clkin_daisy.h"

#include "irq_lock_daisy.h"

namespace clkin {
namespace {

constexpr int64_t kLockoutUs = 1000;
constexpr uint32_t kRingSize = 32;

Event g_ring[kRingSize];
volatile uint32_t g_head = 0;
volatile uint32_t g_tail = 0;
int64_t g_last[2] = {INT64_MIN, INT64_MIN};
bool g_level[2] = {false, false};

daisy::GPIO g_clk;
daisy::GPIO g_rst;

// Producer side runs inside the sampling ISR, so no masking needed here;
// pop() masks around the consumer side.
void capture(Kind kind, int64_t now) {
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

}  // namespace

void init(daisy::Pin clk_pin, daisy::Pin rst_pin) {
  // External input conditioning holds the line low when idle
  // (HARDWARE.md §5.4); no pull needed, but a pulldown keeps a floating
  // bench setup quiet.
  g_clk.Init(clk_pin, daisy::GPIO::Mode::INPUT, daisy::GPIO::Pull::PULLDOWN);
  g_rst.Init(rst_pin, daisy::GPIO::Mode::INPUT, daisy::GPIO::Pull::PULLDOWN);
}

void sample_isr(int64_t now_us) {
  const bool clk = g_clk.Read();
  if (clk && !g_level[0]) {
    capture(Kind::kClock, now_us);
  }
  g_level[0] = clk;
  const bool rst = g_rst.Read();
  if (rst && !g_level[1]) {
    capture(Kind::kReset, now_us);
  }
  g_level[1] = rst;
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
