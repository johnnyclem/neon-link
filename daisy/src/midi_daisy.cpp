#include "midi_daisy.h"

#include "daisy_seed.h"

#include "app_state/config_store.h"
#include "app_state/timeline_bus.h"
#include "neon/midi/midi_encoder.hpp"

#include "board_pins_daisy.h"
#include "irq_lock_daisy.h"
#include "timebase_daisy.h"

namespace miditrs {
namespace {

daisy::UartHandler g_uart;
daisy::TimerHandle g_timer;
bool g_uart_ok = false;

// Staged for the ISR (written IRQ-masked from poll(); a seqlock read
// from the ISR could livelock against a mid-publish main loop on this
// single core).
neon::TimelineSnapshot g_tl{};
volatile int64_t g_next_tick_us = 0;
volatile bool g_clock_on = false;
volatile int32_t g_nudge_us = 0;

// A MIDI byte is ~320 µs on the wire and clock ticks are ≥3 ms apart at
// the tempo ceiling, so the TX register is effectively always free; the
// FIFO-full check just refuses to block the ISR if it ever is not.
inline bool tx_byte(uint8_t b) {
  if ((USART1->ISR & USART_ISR_TXE_TXFNF) == 0) {
    return false;
  }
  USART1->TDR = b;
  return true;
}

// Next 24 PPQN tick strictly after now, on the nudged grid — the same
// solve as the ESP and Teensy midi services.
bool next_clock_tick_us(const neon::TimelineSnapshot& tl, int64_t now_us,
                        int64_t* out) {
  if (tl.tempo_mpb_q32 == 0) {
    return false;
  }
  const int64_t nudge = g_nudge_us;
  const double mpb = static_cast<double>(tl.tempo_mpb_q32) / 4294967296.0;
  const double grid_now = static_cast<double>(now_us - nudge);
  const double b0 = static_cast<double>(tl.beat_at_origin_q32) / 4294967296.0;
  const double beat =
      b0 + (grid_now - static_cast<double>(tl.origin_us)) / mpb;
  const double tick_beats = 1.0 / 24.0;
  const int64_t tick = static_cast<int64_t>(beat / tick_beats) + 1;
  const double target_beat = static_cast<double>(tick) * tick_beats;
  *out = tl.origin_us + nudge +
         static_cast<int64_t>((target_beat - b0) * mpb);
  if (*out <= now_us) {
    *out = now_us + 1000;
  }
  return true;
}

void tick_isr(void*) {
  if (!g_clock_on) {
    return;
  }
  const int64_t now = daisy_now_us();
  if (g_next_tick_us != 0 && now >= g_next_tick_us) {
    tx_byte(neon::midi::kClock);
  } else if (g_next_tick_us != 0) {
    return;
  }
  int64_t next = 0;
  g_next_tick_us = next_clock_tick_us(g_tl, now, &next) ? next : 0;
}

}  // namespace

void init() {
  daisy::UartHandler::Config ucfg;
  ucfg.periph = daisy::UartHandler::Config::Peripheral::USART_1;
  ucfg.mode = daisy::UartHandler::Config::Mode::TX;
  ucfg.baudrate = 31250;
  ucfg.pin_config.tx = kPinMidiTx;
  ucfg.pin_config.rx = kPinMidiRx;
  g_uart_ok = g_uart.Init(ucfg) == daisy::UartHandler::Result::OK;
  if (!g_uart_ok) {
    return;
  }

  daisy::TimerHandle::Config tcfg;
  tcfg.periph = daisy::TimerHandle::Config::Peripheral::TIM_4;
  tcfg.dir = daisy::TimerHandle::Config::CounterDir::UP;
  tcfg.period = 500;
  tcfg.enable_irq = true;
  if (g_timer.Init(tcfg) != daisy::TimerHandle::Result::OK) {
    return;
  }
  // TIM4 is 16-bit: prescale to 1 MHz so the 500-tick period is 500 µs.
  g_timer.SetPrescaler(g_timer.GetFreq() / 1000000 - 1);
  g_timer.SetPeriod(500);
  g_timer.SetCallback(tick_isr, nullptr);
  // Below the pulse emitter (4), above the audio DMA (8) — see
  // pulse_hw_daisy.cpp for the ranking.
  HAL_NVIC_SetPriority(TIM4_IRQn, 6, 0);
  g_timer.Start();
}

void poll(int64_t now_us) {
  (void)now_us;
  if (!g_uart_ok) {
    return;
  }
  const neon::Config& cfg = neon_config();
  const bool enabled =
      cfg.midi_clock_out != 0 &&
      cfg.midi.clock_policy != neon::MidiRouteConfig::ClockPolicy::kReplace;

  neon::TimelineSnapshot tl;
  timeline_bus().read(tl);

  {
    const uint32_t primask = irq_save();
    g_tl = tl;
    g_nudge_us = cfg.midi_nudge_us;
    g_clock_on = enabled;
    irq_restore(primask);
  }

  // Transport bytes follow the session. The IRQ mask keeps the byte from
  // splitting a clock tick's register write.
  static bool last_playing = false;
  const bool playing = tl.playing != 0;
  if (playing != last_playing) {
    if (enabled) {
      const uint32_t primask = irq_save();
      tx_byte(playing ? neon::midi::kStart : neon::midi::kStop);
      irq_restore(primask);
    }
    last_playing = playing;
  }
}

}  // namespace miditrs
