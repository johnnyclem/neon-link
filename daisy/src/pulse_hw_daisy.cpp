#include "pulse_hw_daisy.h"

#include "daisy_seed.h"

#include "board_pins_daisy.h"
#include "timebase_daisy.h"

namespace {

constexpr uint32_t kTickUs = 100;
// Sized so the pre-persist top-up (config_store_daisy.cpp; the QSPI
// sector erase is the worst main-loop stall) fits at sane edge rates:
// 1024 edges covers ~800 ms at up to ~1.2 kHz aggregate edge rate.
constexpr size_t kRingSize = 1024;  // power of two
static_assert((kRingSize & (kRingSize - 1)) == 0, "ring must be 2^n");

hal::PulseEdge g_ring[kRingSize];
volatile uint32_t g_head = 0;  // consumer (ISR)
volatile uint32_t g_tail = 0;  // producer (loop)

volatile uint32_t g_levels = 0;
volatile uint32_t g_late = 0;
volatile uint32_t g_edges = 0;
volatile uint32_t g_late_max_us = 0;
volatile uint64_t g_late_sum_us = 0;

daisy::GPIO g_pins[kNumPulsePins];
daisy::TimerHandle g_timer;

// The level word tracks all six virtual channels (the RUN LED and the
// UI read it) even on boards where only some land on physical pins —
// kPulsePinChannel maps each real jack to its channel bit.
inline void apply_masks(uint32_t set_mask, uint32_t clear_mask) {
  g_levels = (g_levels | set_mask) & ~clear_mask;
  for (int i = 0; i < kNumPulsePins; ++i) {
    const uint32_t bit = 1u << kPulsePinChannel[i];
    if (set_mask & bit) {
      g_pins[i].Write(true);
    }
    if (clear_mask & bit) {
      g_pins[i].Write(false);
    }
  }
}

void pulse_isr(void*) {
  // Calling daisy_now_us() here services the 64-bit wrap extender every
  // tick, so a stalled main loop can never miss the ~21 s tick wrap.
  const int64_t now = daisy_now_us();
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
  neon_daisy_input_sample_isr(now);
}

}  // namespace

bool PulseHwDaisy::init() {
  for (int i = 0; i < kNumPulsePins; ++i) {
    g_pins[i].Init(kPulsePins[i], daisy::GPIO::Mode::OUTPUT);
    g_pins[i].Write(false);
  }

  daisy::TimerHandle::Config cfg;
  cfg.periph = daisy::TimerHandle::Config::Peripheral::TIM_5;
  cfg.dir = daisy::TimerHandle::Config::CounterDir::UP;
  cfg.period = 10000;  // placeholder; corrected below from the real clock
  cfg.enable_irq = true;
  if (g_timer.Init(cfg) != daisy::TimerHandle::Result::OK) {
    return false;
  }
  g_timer.SetPeriod(g_timer.GetFreq() / (1000000 / kTickUs));
  g_timer.SetCallback(pulse_isr, nullptr);

  // libDaisy parks every TIM at the lowest NVIC priority and the audio
  // SAI DMA at the highest. Re-rank so the edge emitter preempts the
  // audio render, as on the Teensy (pulse 4 < MIDI 6 < audio DMA 8;
  // lower number = higher priority).
  HAL_NVIC_SetPriority(TIM5_IRQn, 4, 0);

  return g_timer.Start() == daisy::TimerHandle::Result::OK;
}

bool PulseHwDaisy::submit(const hal::PulseEdge& e) {
  if (g_tail - g_head >= kRingSize) {
    return false;
  }
  g_ring[g_tail & (kRingSize - 1)] = e;
  // Publish after the payload is in place. The ISR only reads entries
  // strictly below g_tail, and the single-core ARMv7-M memory model keeps
  // program order visible to the interrupt handler.
  asm volatile("dmb" ::: "memory");
  g_tail = g_tail + 1;
  return true;
}

int64_t PulseHwDaisy::now_us() const { return daisy_now_us(); }

uint32_t PulseHwDaisy::levels() { return g_levels; }

uint32_t PulseHwDaisy::edges() { return g_edges; }

uint32_t PulseHwDaisy::late_max_us() { return g_late_max_us; }

uint32_t PulseHwDaisy::late_avg_us() {
  const uint32_t n = g_edges;
  return n != 0 ? static_cast<uint32_t>(g_late_sum_us / n) : 0;
}

uint32_t PulseHwDaisy::late_edges() { return g_late; }
