#include "neon/multi_engine.hpp"

namespace neon {

namespace {

bool role_is_reset(OutputRole r) {
  return r == OutputRole::kResetLoop || r == OutputRole::kResetStart ||
         r == OutputRole::kResetStop;
}

uint32_t sane_quantum(uint32_t q) { return q != 0 ? (q > 16 ? 16 : q) : 4; }

}  // namespace

int32_t MultiClockEngine::reset_latency() const {
  int32_t lat = cfg_.latency_us;
  if (cfg_.reset_before_edge) {
    lat -= static_cast<int32_t>(cfg_.reset_lead_us);
  }
  return lat;
}

ClockOutputConfig MultiClockEngine::effective_clock(int index,
                                                    uint32_t quantum) const {
  ClockOutputConfig c = cfg_.clocks[index];
  if (c.role == OutputRole::kResetLoop) {
    // One trigger per loop, on the loop boundary.
    c.ppqn = 1;
    c.mult = 1;
    c.div = quantum;
    c.mode = ClockOutputConfig::PulseMode::kTrigger;
    c.shuffle_pct = 0;
    c.humanize_pct = 0;
    c.rhythm = ClockOutputConfig::RhythmMode::kAll;
    return c;
  }
  if (c.role != OutputRole::kClock) {
    // Gate / one-shot reset roles produce no periodic edges.
    c.enabled = false;
    return c;
  }
  // Rhythm Explorer "steps across the loop": the pattern's step count
  // spans one quantum instead of riding the ppqn grid.
  if (c.rhythm_over_loop && c.rhythm != ClockOutputConfig::RhythmMode::kAll) {
    c.ppqn = c.euclid_steps != 0 ? c.euclid_steps : 1;
    c.mult = 1;
    c.div = quantum;
  }
  return c;
}

bool MultiClockEngine::channel_runs(int index) const {
  const ClockOutputConfig& c = cfg_.clocks[index];
  if (c.role == OutputRole::kResetLoop) {
    return playing_;  // reset triggers never fire while stopped
  }
  if (c.role != OutputRole::kClock) {
    return false;
  }
  return c.free_run || !cfg_.transport_gating || playing_;
}

void MultiClockEngine::push_pending(int64_t t_us, uint8_t channel, bool high) {
  for (auto& p : pending_) {
    if (!p.valid) {
      p = Pending{t_us, channel, high, true};
      return;
    }
  }
  // Full: drop the newest rather than reorder the stream. Only reachable
  // if the consumer stops calling generate() across many transitions.
}

void MultiClockEngine::emit_reset_pulse(int64_t t_us, uint8_t channel,
                                        uint32_t len_us) {
  const int64_t rise = t_us + reset_latency();
  push_pending(rise, channel, true);
  push_pending(rise + static_cast<int64_t>(len_us), channel, false);
}

void MultiClockEngine::set_config(const EngineConfig& cfg) {
  cfg_ = cfg;
  const uint32_t quantum = sane_quantum(
      tl_.quantum_beats != 0 ? tl_.quantum_beats : cfg_.quantum_beats);
  cfg_.quantum_beats = quantum;

  for (int i = 0; i < 4; ++i) {
    clocks_[i].configure(effective_clock(i, quantum));
    clocks_[i].set_latency(role_is_reset(cfg_.clocks[i].role) ? reset_latency()
                                                              : cfg_.latency_us);
  }

  ClockOutputConfig bar;
  bar.ppqn = 1;
  bar.mult = 1;
  bar.div = quantum;
  bar.mode = ClockOutputConfig::PulseMode::kTrigger;
  bar.trig_len_us = cfg_.reset_trig_len_us;
  bar.enabled = cfg_.reset_mode == ResetMode::kEveryBar;
  reset_bar_.configure(bar);
  reset_bar_.set_latency(reset_latency());
}

void MultiClockEngine::sync_levels(int64_t from_us) {
  for (int i = 0; i < 4; ++i) {
    const ClockOutputConfig& c = cfg_.clocks[i];
    const bool want =
        c.enabled && c.role == OutputRole::kGate && playing_;
    if (want != gate_level_[i]) {
      push_pending(from_us + cfg_.latency_us, static_cast<uint8_t>(i), want);
      gate_level_[i] = want;
    }
  }
  const bool want_run = cfg_.run_enabled && playing_;
  if (want_run != run_target_) {
    push_pending(from_us + cfg_.latency_us, kChRun, want_run);
    run_target_ = want_run;
  }
}

void MultiClockEngine::retime(const TimelineSnapshot& tl, int64_t from_us) {
  const bool was_playing = playing_;
  playing_ = tl.playing != 0;

  // Quantum changes affect every loop-rate channel's divider.
  const bool quantum_changed = tl.quantum_beats != tl_.quantum_beats;
  tl_ = tl;
  if (quantum_changed || !have_timeline_) {
    set_config(cfg_);
  }
  have_timeline_ = true;

  for (int i = 0; i < 4; ++i) {
    clocks_[i].retime(tl, from_us, channel_runs(i));
  }
  reset_bar_.retime(tl, from_us,
                    playing_ && cfg_.reset_mode == ResetMode::kEveryBar);

  // Gate and Run levels are re-driven every retime, so re-assigning a role
  // (or enabling/disabling an output) takes effect at once rather than
  // leaving the jack stuck at whatever the last transport change set.
  sync_levels(from_us);

  if (playing_ == was_playing) {
    return;
  }

  // --- One-shot reset pulses on transport transitions ---------------
  if (playing_ && cfg_.reset_mode == ResetMode::kStartOfPlay) {
    emit_reset_pulse(from_us, kChReset, cfg_.reset_trig_len_us);
  } else if (!playing_ && cfg_.reset_mode == ResetMode::kAtStop) {
    emit_reset_pulse(from_us, kChReset, cfg_.reset_trig_len_us);
  }

  for (int i = 0; i < 4; ++i) {
    const ClockOutputConfig& c = cfg_.clocks[i];
    if (!c.enabled) {
      continue;
    }
    const uint8_t ch = static_cast<uint8_t>(i);
    if (c.role == OutputRole::kResetStart && playing_) {
      emit_reset_pulse(from_us, ch, c.trig_len_us);
    } else if (c.role == OutputRole::kResetStop && !playing_) {
      emit_reset_pulse(from_us, ch, c.trig_len_us);
    }
  }
}

size_t MultiClockEngine::generate(int64_t t0_us, int64_t t1_us, Edge* out,
                                  size_t max_out) {
  size_t n = 0;
  while (n < max_out) {
    // Find the earliest pending edge across all sources.
    int64_t best_t = INT64_MAX;
    int best_src = -1;  // 0..3 clocks, 4 bar-reset, 5 one-shot queue
    int best_pending = -1;
    Edge e{};

    for (int i = 0; i < 4; ++i) {
      Edge c;
      if (clocks_[i].peek(&c) && c.t_us < best_t) {
        best_t = c.t_us;
        best_src = i;
        e = c;
        e.channel = static_cast<uint8_t>(i);
      }
    }
    {
      Edge c;
      if (reset_bar_.peek(&c) && c.t_us < best_t) {
        best_t = c.t_us;
        best_src = 4;
        e = c;
        e.channel = kChReset;
      }
    }
    for (size_t i = 0; i < kMaxPending; ++i) {
      const Pending& p = pending_[i];
      if (p.valid && p.t_us < best_t) {
        best_t = p.t_us;
        best_src = 5;
        best_pending = static_cast<int>(i);
        e = Edge{p.t_us, p.channel, p.high};
      }
    }

    if (best_src < 0 || best_t >= t1_us) {
      break;
    }

    // Consume from the winning source.
    if (best_src == 5) {
      pending_[best_pending].valid = false;
      if (e.channel == kChRun) {
        run_level_ = e.high;
      }
      // One-shots are events, not grid positions: a reset pulse or a gate
      // transition scheduled just before this window (reset_before_edge
      // leads the anchor, and the anchor is usually t0 itself) must still
      // fire. Clamp it into the window rather than dropping it, which
      // would swallow the pulse entirely or strand a gate at the wrong
      // level. Periodic channels keep the consume-silently rule below.
      if (e.t_us < t0_us) {
        e.t_us = t0_us;
      }
      out[n++] = e;
      continue;
    }
    if (best_src == 4) {
      reset_bar_.pop();
    } else {
      clocks_[best_src].pop();
    }

    if (e.t_us >= t0_us) {
      out[n++] = e;
    }
  }
  return n;
}

}  // namespace neon
