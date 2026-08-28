#pragma once

// The policy layer between the MIDI clock PLL and a session owner
// (docs/SPIKE_MIDI_PLL.md §5.4): publish hysteresis, correction rate
// limiting, and clean handover when a higher-priority source (CLK IN)
// takes or releases the clock. Pure logic, host-tested — the service
// glue on each target reduces to "drain events in, apply actions out".

#include <cstdint>

#include "neon/midi/clock_pll.hpp"

namespace neon {
namespace midi {

// One queued sync event, produced wherever MIDI arrives (BLE packet
// drain, UART drain) and consumed by whichever task owns the session.
// Small and POD so it fits the app_state lock-free ring idiom.
struct SyncEvent {
  enum class Kind : uint8_t {
    kTick = 0,      // 0xF8
    kStart = 1,     // 0xFA
    kContinue = 2,  // 0xFB
    kStop = 3,      // 0xFC
    kSpp = 4,       // 0xF2 (t_us unused, spp carries the position)
  };
  Kind kind = Kind::kTick;
  MidiClockPll::Transport transport = MidiClockPll::Transport::kDin;
  int64_t t_us = 0;
  uint16_t spp = 0;
};

class SyncFollower {
 public:
  // What the session owner should do this poll. Tempo publishes are
  // hysteretic (0.5 % band, min 1 s apart — the same "robust following
  // without setTempo spam" contract the CLK IN path keeps); transport
  // and downbeat corrections are edges.
  struct Actions {
    bool following = false;
    bool set_tempo = false;
    uint32_t tempo_mbpm = 0;
    bool anchor_downbeat = false;
    int64_t downbeat_us = 0;
    bool set_playing = false;
    bool playing = false;
  };

  static constexpr int64_t kTempoGapUs = 1000000;
  static constexpr int64_t kHysteresisDen = 200;  // 0.5 %

  void on_event(const SyncEvent& ev);

  // allowed carries the arbiter's verdict for this poll: the config
  // permits MIDI as a source and nothing that outranks it (CLK IN) is
  // live. While not allowed the PLL keeps tracking silently, so a later
  // handover starts from a warm estimate.
  Actions poll(int64_t now_us, bool allowed);

  const MidiClockPll& pll() const { return pll_; }

 private:
  MidiClockPll pll_;
  bool following_ = false;
  uint32_t published_mbpm_ = 0;
  int64_t last_tempo_us_ = 0;
  bool have_playing_ = false;
  bool sent_playing_ = false;
};

}  // namespace midi
}  // namespace neon
