#include "pulse_hw_daisy.h"

#include "daisy_seed.h"

#include "board_pins_daisy.h"
#include "irq_lock_daisy.h"
#include "timebase_daisy.h"

namespace {

// CC2 cadence: input sampling + timebase wrap service + arming sweep.
constexpr uint32_t kSampleTickUs = 100;
// CC1 is armed only for edges due inside this window; anything further
// out waits for a later sweep (keeps the compare target comfortably
// ahead of the counter).
constexpr int64_t kArmWindowUs = 150;
// An edge due closer than this is emitted from the current interrupt
// (spinning out the last microsecond or two) instead of re-armed — a
// compare target this near the counter could be missed.
constexpr int64_t kSpinUs = 3;
// Emission later than this counts as a late edge. The polled
// predecessor used its 100 µs tick here; the compare emitter is held to
// interrupt-latency standards.
constexpr int64_t kLateThresholdUs = 10;

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

// Emits every edge that is due (or nearly due), then arms CC1 for the
// next one if it falls inside the window. ISR context only.
void emit_due_and_arm() {
  int64_t now = daisy_now_us();
  while (g_head != g_tail) {
    const hal::PulseEdge& e = g_ring[g_head & (kRingSize - 1)];
    const int64_t delta = e.t_us - now;
    if (delta > kSpinUs) {
      break;
    }
    // Almost due: spin out the last microseconds so the write lands on
    // the scheduled time instead of early. Bounded by kSpinUs.
    while (e.t_us > daisy_now_us()) {
    }
    now = daisy_now_us();
    const int64_t late = now - e.t_us;
    if (late > kLateThresholdUs) {
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

  if (g_head != g_tail) {
    const int64_t delta =
        g_ring[g_head & (kRingSize - 1)].t_us - daisy_now_us();
    if (delta <= kArmWindowUs) {
      // delta > kSpinUs here (the loop above consumed anything nearer),
      // so the compare target sits safely ahead of the counter; the
      // EGR guard below covers the pathological case anyway.
      TIM5->CCR1 = TIM5->CNT + static_cast<uint32_t>(delta);
      TIM5->SR = ~TIM_FLAG_CC1;  // W0C: drop any stale match
      TIM5->DIER |= TIM_IT_CC1;
      if (static_cast<int32_t>(TIM5->CCR1 - TIM5->CNT) <= 0) {
        TIM5->EGR = TIM_EGR_CC1G;  // already passed: fire it by hand
      }
      return;
    }
  }
  TIM5->DIER &= ~TIM_IT_CC1;
}

void sample_tick() {
  // Re-arm from the live counter (not CCR2 += period): if this
  // interrupt was ever held off past a whole period, an incremental
  // target could land behind the counter and not match again until the
  // 32-bit wrap 71 minutes later.
  TIM5->CCR2 = TIM5->CNT + kSampleTickUs;
  if (static_cast<int32_t>(TIM5->CCR2 - TIM5->CNT) <= 0) {
    TIM5->EGR = TIM_EGR_CC2G;
  }
  // Calling daisy_now_us() here services the 64-bit wrap extender every
  // tick, so a stalled main loop can never miss the ~21 s tick wrap.
  const int64_t now = daisy_now_us();
  emit_due_and_arm();
  neon_daisy_input_sample_isr(now);
}

}  // namespace

// libDaisy routes TIM5's interrupt through HAL_TIM_IRQHandler, which
// dispatches compare events here (the weak HAL callback; libDaisy does
// not define it). CC1 = the precise edge shot, CC2 = the 10 kHz
// housekeeping tick.
extern "C" void HAL_TIM_OC_DelayElapsedCallback(TIM_HandleTypeDef* htim) {
  if (htim->Instance != TIM5) {
    return;
  }
  if (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_1) {
    emit_due_and_arm();
  } else if (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_2) {
    sample_tick();
  }
}

bool PulseHwDaisy::init() {
  for (int i = 0; i < kNumPulsePins; ++i) {
    g_pins[i].Init(kPulsePins[i], daisy::GPIO::Mode::OUTPUT);
    g_pins[i].Write(false);
  }

  // TimerHandle does the base bring-up (clock, NVIC, UIE at the 32-bit
  // wrap ~71 min out, harmlessly unhandled); the compare channels are
  // driven at the register level because the wrapper does not model
  // them.
  daisy::TimerHandle::Config cfg;
  cfg.periph = daisy::TimerHandle::Config::Peripheral::TIM_5;
  cfg.dir = daisy::TimerHandle::Config::CounterDir::UP;
  cfg.period = 0xffffffff;  // free-running
  cfg.enable_irq = true;
  if (g_timer.Init(cfg) != daisy::TimerHandle::Result::OK) {
    return false;
  }
  // 1 MHz: one counter tick = one microsecond, so CCR deltas are the
  // same numbers the µs timebase deals in.
  g_timer.SetPrescaler(g_timer.GetFreq() / 1000000 - 1);

  // libDaisy parks every TIM at the lowest NVIC priority and the audio
  // SAI DMA at the highest. Re-rank so the edge emitter preempts the
  // audio render, as on the Teensy (pulse 4 < MIDI 6 < audio DMA 8;
  // lower number = higher priority).
  HAL_NVIC_SetPriority(TIM5_IRQn, 4, 0);

  if (g_timer.Start() != daisy::TimerHandle::Result::OK) {
    return false;
  }
  TIM5->CCR2 = TIM5->CNT + kSampleTickUs;
  TIM5->SR = ~(TIM_FLAG_CC1 | TIM_FLAG_CC2);
  TIM5->DIER |= TIM_IT_CC2;
  return true;
}

bool PulseHwDaisy::submit(const hal::PulseEdge& e) {
  if (g_tail - g_head >= kRingSize) {
    return false;
  }
  g_ring[g_tail & (kRingSize - 1)] = e;
  // Publish after the payload is in place. The ISR only reads entries
  // strictly below g_tail, and the single-core ARMv7-M memory model keeps
  // program order visible to the interrupt handler. No timer interplay:
  // the ≥2 ms scheduling lead means the next housekeeping tick arms the
  // shot long before the edge is due.
  asm volatile("dmb" ::: "memory");
  g_tail = g_tail + 1;
  return true;
}

int64_t PulseHwDaisy::now_us() const { return daisy_now_us(); }

uint32_t PulseHwDaisy::levels() { return g_levels; }

void PulseHwDaisy::set_level_now(uint8_t channel, bool on) {
  const uint32_t mask = 1u << channel;
  const uint32_t primask = irq_save();
  apply_masks(on ? mask : 0u, on ? 0u : mask);
  irq_restore(primask);
}

uint32_t PulseHwDaisy::edges() { return g_edges; }

uint32_t PulseHwDaisy::late_max_us() { return g_late_max_us; }

uint32_t PulseHwDaisy::late_avg_us() {
  const uint32_t n = g_edges;
  return n != 0 ? static_cast<uint32_t>(g_late_sum_us / n) : 0;
}

uint32_t PulseHwDaisy::late_edges() { return g_late; }
