#include "midi_daisy.h"

#include "daisy_seed.h"

#include "app_state/audio_bus.h"
#include "app_state/config_store.h"
#include "app_state/timeline_bus.h"
#include "neon/midi/clock_engine.hpp"
#include "neon/midi/midi_encoder.hpp"
#include "neon/midi/router.hpp"
#include "neon/midi/serial_midi_parser.hpp"

#include "board_daisy.h"
#include "board_pins_daisy.h"
#include "irq_lock_daisy.h"
#include "timebase_daisy.h"

namespace miditrs {
namespace {

daisy::UartHandler g_uart;     // MIDI out (and in, when the board shares)
daisy::UartHandler g_uart_in;  // separate MIDI-in UART (Pod only)
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
  if ((NEON_MIDI_UART_REGS->ISR & USART_ISR_TXE_TXFNF) == 0) {
    return false;
  }
  NEON_MIDI_UART_REGS->TDR = b;
  return true;
}

// The link service's MIDI clock follower (it owns the arbitration and
// the session; registration in linksvc::init). Both the UART drain and
// the follower's consumer run on the main loop, so the sync tap calls
// on_event directly.
neon::midi::SyncFollower* g_sync = nullptr;

// --- Router sink: routing decisions become module actions -------------
// Mirrors the ESP midi service's sink, minus what this hardware lacks.

class Sink final : public neon::IRouterSink {
 public:
  void gate(uint8_t target, bool on) override {
    GateEvent ev;
    ev.channel = target == neon::MidiRouteConfig::kTargetRun
                     ? static_cast<uint8_t>(neon::kChRun)
                     : target;
    ev.on = on;
    gate_queue_push(ev);  // the main loop applies it via set_level_now
  }
  void note(uint8_t, uint8_t, bool) override {
    // No synth voice on this target (the AMY engine is ESP32-only).
  }
  void all_notes_off() override {}
  void pitch_cv(uint16_t ratio_q16) override {
    // The router only calls this when midi.pitch_cv is set, and the
    // link service yields the jack for the same flag.
    board_tempo_cv_write(ratio_q16);
  }
  void latency_offset(int32_t latency_us) override {
    neon::Config cfg = neon_config();
    cfg.engine.latency_us = latency_us;
    neon_config_apply(cfg);
  }
  void shuffle(uint8_t clock_index, uint8_t pct) override {
    neon::Config cfg = neon_config();
    cfg.engine.clocks[clock_index & 3].shuffle_pct = pct;
    neon_config_apply(cfg);
  }
  void transport(bool play) override {
    // The timeline has a single owner (the link service); route the
    // request through the control queue rather than touching it here.
    ControlCommand cmd{};
    cmd.kind = play ? ControlCommand::Kind::kPlayNow
                    : ControlCommand::Kind::kStopNow;
    cmd.from_midi = 1;
    control_queue_push(cmd);
  }
  void trs_realtime(uint8_t status) override {
    // The IRQ mask keeps the byte from splitting a clock tick's
    // register write.
    const uint32_t primask = irq_save();
    tx_byte(status);
    irq_restore(primask);
  }
  void program_change(uint8_t program) override {
    neon_preset_recall(program % kPresetSlots);
  }

  // Clock-sync tap -> the link service's SyncFollower (this input is a
  // TRS UART, so the PLL runs the DIN gain set and the BLE sender stamp
  // is always kNoSenderMs).
  void midi_clock_byte(uint8_t status, int64_t t_us,
                       uint16_t sender_ms13) override {
    if (g_sync == nullptr) {
      return;
    }
    neon::midi::SyncEvent ev;
    ev.sender_ms13 = sender_ms13;
    switch (status) {
      case neon::midi::kClock:
        ev.kind = neon::midi::SyncEvent::Kind::kTick;
        break;
      case neon::midi::kStart:
        ev.kind = neon::midi::SyncEvent::Kind::kStart;
        break;
      case neon::midi::kContinue:
        ev.kind = neon::midi::SyncEvent::Kind::kContinue;
        break;
      case neon::midi::kStop:
        ev.kind = neon::midi::SyncEvent::Kind::kStop;
        break;
      default:
        return;
    }
    ev.transport = neon::MidiClockPll::Transport::kDin;
    ev.t_us = t_us;
    g_sync->on_event(ev);
  }
  void midi_song_position(uint16_t sixteenths) override {
    if (g_sync == nullptr) {
      return;
    }
    neon::midi::SyncEvent ev;
    ev.kind = neon::midi::SyncEvent::Kind::kSpp;
    ev.transport = neon::MidiClockPll::Transport::kDin;
    ev.spp = sixteenths;
    g_sync->on_event(ev);
  }
};

Sink g_sink;
neon::MidiRouter g_router({}, &g_sink);
neon::SerialMidiParser g_parser(&g_router);

// Next 24 PPQN tick strictly after now, on the nudged grid — the shared
// integer solve (neon/midi/clock_engine.hpp), safe in the ISR.
bool next_clock_tick_us(const neon::TimelineSnapshot& tl, int64_t now_us,
                        int64_t* out) {
  return neon::midi::next_nudged_clock_us(tl, now_us, g_nudge_us, out);
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

// Drain the RX data register from the main loop. At 31250 baud a byte
// is 320 µs on the wire and the loop passes far more often than that;
// the one real gap is the blocking QSPI persist (~100 ms worst), where
// dropped bytes cost a resynced parser and a PLL residual spike — both
// self-healing. The parser timestamps every byte with the drain time;
// realtime clock bytes reach the follower through the router sync tap.
void drain_rx(USART_TypeDef* regs, int64_t now_us) {
  // A latched overrun/framing/noise error blocks further reception
  // until cleared.
  if (regs->ISR & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE)) {
    regs->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF;
  }
  while (regs->ISR & USART_ISR_RXNE_RXFNE) {
    const uint8_t b = static_cast<uint8_t>(regs->RDR);
    g_parser.feed(b, now_us);
  }
}

}  // namespace

void init() {
  daisy::UartHandler::Config ucfg;
  ucfg.periph = kMidiUartPeriph;
  ucfg.mode = kMidiInSharedUart ? daisy::UartHandler::Config::Mode::TX_RX
                                : daisy::UartHandler::Config::Mode::TX;
  ucfg.baudrate = 31250;
  ucfg.pin_config.tx = kPinMidiTx;
  ucfg.pin_config.rx = kPinMidiRx;
  g_uart_ok = g_uart.Init(ucfg) == daisy::UartHandler::Result::OK;
  if (!g_uart_ok) {
    return;
  }

  if (!kMidiInSharedUart) {
    // Separate RX-only UART (the Pod's own MIDI IN jack). The tx pin is
    // left at PORTX so its real owner (the encoder click) is untouched.
    daisy::UartHandler::Config icfg;
    icfg.periph = kMidiInPeriph;
    icfg.mode = daisy::UartHandler::Config::Mode::RX;
    icfg.baudrate = 31250;
    icfg.pin_config.tx = daisy::Pin();
    icfg.pin_config.rx = kPinMidiIn;
    g_uart_in.Init(icfg);
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

  g_router.set_config(cfg.midi);
  drain_rx(NEON_MIDI_IN_UART_REGS, now_us);

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

void set_sync_follower(neon::midi::SyncFollower* follower) {
  g_sync = follower;
}

}  // namespace miditrs
