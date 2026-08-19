#pragma once

// Link-derived MIDI clock + transport. Portable, host-tested, integer
// beat math only (no doubles). The firmware schedules the events through
// the GPTimer edge ring; this type does not touch UART or Link.

#include <cstddef>
#include <cstdint>

#include "neon/midi/midi_encoder.hpp"
#include "neon/timeline.hpp"

namespace neon {
namespace midi {

enum class EventKind : uint8_t {
  Clock = 0,
  Start = 1,
  Cont = 2,
  Stop = 3,
  Spp = 4,
};

struct Event {
  int64_t t_us = 0;
  EventKind kind = EventKind::Clock;
  uint16_t spp = 0;  // 14-bit sixteenths; valid when kind == Spp
};

// Encode one event into 1 or 3 bytes. buf must hold >= 3.
size_t encode_event(const Event& e, uint8_t* buf);

// MIDI song position: floor(beat * 4) sixteenths, wrapped to 14 bits.
uint16_t song_position_16ths(const TimelineSnapshot& tl, int64_t t_us);

// First 24-PPQN clock strictly after t_us, on the session grid.
int64_t next_clock_us(const TimelineSnapshot& tl, int64_t t_us);

// Produces timed MIDI realtime events from TimelineSnapshot.
//
// Transport:
//   play rising, SPP == 0  → Start just before the next clock
//   play rising, SPP != 0  → SPP + Continue, then clocks (join mid-song)
//   play falling           → Stop immediately
//
// Clocks fire on the 24 PPQN grid while playing. midi_nudge_us shifts
// every emitted time the same way the module's TRS clock already does.
class ClockEngine {
 public:
  void set_nudge(int32_t nudge_us) { nudge_us_ = nudge_us; }
  int32_t nudge() const { return nudge_us_; }

  void retime(const TimelineSnapshot& tl, int64_t now_us);

  // Events in (from_us, until_us], in time order. Returns count written.
  size_t generate(int64_t from_us, int64_t until_us, Event* out, size_t cap);

  bool playing() const { return playing_; }
  bool have_timeline() const { return have_tl_; }

 private:
  void emit(Event* out, size_t* n, size_t cap, int64_t t_us, EventKind kind,
            uint16_t spp);

  TimelineSnapshot tl_{};
  bool have_tl_ = false;
  bool playing_ = false;
  int32_t nudge_us_ = 0;
  int64_t next_clock_us_ = 0;
  bool pending_join_ = false;
  bool pending_stop_ = false;
  uint16_t join_spp_ = 0;
};

}  // namespace midi
}  // namespace neon
