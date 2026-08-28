#include "neon/midi/clock_engine.hpp"

#include "neon/transport.hpp"

namespace neon {
namespace midi {

namespace {

// floor(beat_q32 * ppqn / 2^32) with correct floor for negatives.
int64_t floor_beats_times(int64_t beat_q32, int32_t ppqn) {
  if (beat_q32 >= 0) {
    return static_cast<int64_t>(
        (static_cast<uint64_t>(beat_q32) * static_cast<uint64_t>(ppqn)) >> 32);
  }
  const uint64_t mag = static_cast<uint64_t>(-beat_q32);
  const uint64_t prod = mag * static_cast<uint64_t>(ppqn);
  int64_t q = static_cast<int64_t>(prod >> 32);
  if ((prod & 0xffffffffull) != 0) {
    ++q;
  }
  return -q;
}

int64_t beat_q32_from_clocks(int64_t clocks, int32_t ppqn) {
  if (clocks >= 0) {
    return static_cast<int64_t>((static_cast<uint64_t>(clocks) << 32) /
                                static_cast<uint64_t>(ppqn));
  }
  return -static_cast<int64_t>((static_cast<uint64_t>(-clocks) << 32) /
                               static_cast<uint64_t>(ppqn));
}

}  // namespace

size_t encode_event(const Event& e, uint8_t* buf) {
  switch (e.kind) {
    case EventKind::Clock:
      buf[0] = kClock;
      return 1;
    case EventKind::Start:
      buf[0] = kStart;
      return 1;
    case EventKind::Cont:
      buf[0] = kContinue;
      return 1;
    case EventKind::Stop:
      buf[0] = kStop;
      return 1;
    case EventKind::Spp:
      return song_position(e.spp, buf);
  }
  return 0;
}

uint16_t song_position_16ths(const TimelineSnapshot& tl, int64_t t_us) {
  const int64_t beat_q32 = beat_at_q32(tl, t_us);
  const int64_t sixteenths = floor_beats_times(beat_q32, 4);
  if (sixteenths < 0) {
    return 0;
  }
  return static_cast<uint16_t>(sixteenths & 0x3fff);
}

int64_t next_clock_us(const TimelineSnapshot& tl, int64_t t_us) {
  if (tl.tempo_mpb_q32 == 0) {
    return t_us + 1000;
  }
  const int64_t beat_q32 = beat_at_q32(tl, t_us);
  int64_t next = floor_beats_times(beat_q32, 24) + 1;
  // Inverse beat↔time rounding can map clock k back onto t_us; step the
  // index, not the time, so we never emit a 1 µs "clock" storm.
  for (int i = 0; i < 4; ++i) {
    const int64_t t_next =
        time_at_beat_q32(tl, beat_q32_from_clocks(next, 24));
    if (t_next > t_us) {
      return t_next;
    }
    ++next;
  }
  return t_us + 1;
}

bool next_nudged_clock_us(const TimelineSnapshot& tl, int64_t now_us,
                          int64_t nudge_us, int64_t* out) {
  if (tl.tempo_mpb_q32 == 0) {
    return false;
  }
  *out = next_clock_us(tl, now_us - nudge_us) + nudge_us;
  return true;
}

void ClockEngine::emit(Event* out, size_t* n, size_t cap, int64_t t_us,
                       EventKind kind, uint16_t spp) {
  if (*n >= cap) {
    return;
  }
  out[*n].t_us = t_us + nudge_us_;
  out[*n].kind = kind;
  out[*n].spp = spp;
  ++(*n);
}

void ClockEngine::retime(const TimelineSnapshot& tl, int64_t now_us) {
  const bool was_playing = playing_;
  tl_ = tl;
  have_tl_ = tl.tempo_mpb_q32 != 0;
  playing_ = have_tl_ && tl.playing != 0;
  next_clock_us_ = have_tl_ ? next_clock_us(tl_, now_us) : now_us + 1000;

  if (playing_ && !was_playing) {
    pending_join_ = true;
    pending_stop_ = false;
    join_spp_ = song_position_16ths(tl_, now_us);
  } else if (!playing_ && was_playing) {
    pending_stop_ = true;
    pending_join_ = false;
  }
}

size_t ClockEngine::generate(int64_t from_us, int64_t until_us, Event* out,
                             size_t cap) {
  if (out == nullptr || cap == 0 || until_us <= from_us) {
    return 0;
  }
  size_t n = 0;

  if (pending_stop_) {
    emit(out, &n, cap, from_us + 1, EventKind::Stop, 0);
    pending_stop_ = false;
  }

  if (pending_join_ && next_clock_us_ <= until_us) {
    const bool start = join_spp_ == 0;
    const size_t need = start ? 1u : 2u;
    if (n + need <= cap) {
      if (start) {
        int64_t t = next_clock_us_ - 400;
        if (t <= from_us) {
          t = from_us + 1;
        }
        emit(out, &n, cap, t, EventKind::Start, 0);
      } else {
        int64_t t_spp = next_clock_us_ - 1600;
        int64_t t_cont = next_clock_us_ - 400;
        if (t_spp <= from_us) {
          t_spp = from_us + 1;
        }
        if (t_cont <= t_spp) {
          t_cont = t_spp + 1;
        }
        emit(out, &n, cap, t_spp, EventKind::Spp, join_spp_);
        emit(out, &n, cap, t_cont, EventKind::Cont, 0);
      }
      pending_join_ = false;
    }
  }

  if (!playing_ || !have_tl_) {
    return n;
  }

  int64_t t = next_clock_us_;
  while (t <= until_us && n < cap) {
    if (t > from_us) {
      emit(out, &n, cap, t, EventKind::Clock, 0);
    }
    t = next_clock_us(tl_, t);
  }
  next_clock_us_ = t;
  return n;
}

}  // namespace midi
}  // namespace neon
