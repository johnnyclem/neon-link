#include "neon/multi_engine.hpp"

namespace neon {

void MultiClockEngine::set_config(const EngineConfig& cfg) {
  cfg_ = cfg;
  for (int i = 0; i < 4; ++i) {
    clocks_[i].configure(cfg_.clocks[i]);
    clocks_[i].set_latency(cfg_.latency_us);
  }
  ClockOutputConfig bar;
  bar.ppqn = 1;
  bar.mult = 1;
  bar.div = tl_.quantum_beats != 0 ? tl_.quantum_beats : 4;
  bar.mode = ClockOutputConfig::PulseMode::kTrigger;
  bar.trig_len_us = cfg_.reset_trig_len_us;
  bar.enabled = cfg_.reset_mode == ResetMode::kEveryBar;
  reset_bar_.configure(bar);
  reset_bar_.set_latency(cfg_.latency_us);
  if (have_timeline_) {
    apply_config(0);  // re-anchor lazily on the next retime
  }
}

void MultiClockEngine::apply_config(int64_t) {}

void MultiClockEngine::retime(const TimelineSnapshot& tl, int64_t from_us) {
  const bool was_playing = playing_;
  playing_ = tl.playing != 0;

  // Quantum changes affect the bar-reset channel's divider.
  if (tl.quantum_beats != tl_.quantum_beats) {
    tl_ = tl;
    set_config(cfg_);
  } else {
    tl_ = tl;
  }
  have_timeline_ = true;

  const bool clocks_running = cfg_.transport_gating ? playing_ : true;
  for (int i = 0; i < 4; ++i) {
    clocks_[i].retime(tl, from_us, clocks_running);
  }
  reset_bar_.retime(tl, from_us,
                    playing_ && cfg_.reset_mode == ResetMode::kEveryBar);

  // Transport transitions -> Run edge and StartOfPlay Reset pulse.
  if (playing_ != was_playing && cfg_.run_enabled) {
    pending_run_edge_ = from_us;
    pending_run_high_ = playing_;
  }
  if (playing_ && !was_playing && cfg_.reset_mode == ResetMode::kStartOfPlay) {
    pending_reset_rise_ = from_us;
    pending_reset_fall_ = from_us + static_cast<int64_t>(cfg_.reset_trig_len_us);
  }
}

size_t MultiClockEngine::generate(int64_t t0_us, int64_t t1_us, Edge* out,
                                  size_t max_out) {
  size_t n = 0;
  while (n < max_out) {
    // Find the earliest pending edge across all sources.
    int64_t best_t = INT64_MAX;
    int best_src = -1;  // 0..3 clocks, 4 bar-reset, 5 run one-shot,
                        // 6 reset rise one-shot, 7 reset fall one-shot
    Edge e;

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
    if (pending_run_edge_ != kNone) {
      const int64_t t = pending_run_edge_ + cfg_.latency_us;
      if (t < best_t) {
        best_t = t;
        best_src = 5;
        e = Edge{t, kChRun, pending_run_high_};
      }
    }
    if (pending_reset_rise_ != kNone) {
      const int64_t t = pending_reset_rise_ + cfg_.latency_us;
      if (t < best_t) {
        best_t = t;
        best_src = 6;
        e = Edge{t, kChReset, true};
      }
    }
    if (pending_reset_fall_ != kNone && pending_reset_rise_ == kNone) {
      const int64_t t = pending_reset_fall_ + cfg_.latency_us;
      if (t < best_t) {
        best_t = t;
        best_src = 7;
        e = Edge{t, kChReset, false};
      }
    }

    if (best_src < 0 || best_t >= t1_us) {
      break;
    }

    // Consume from the winning source.
    switch (best_src) {
      case 4:
        reset_bar_.pop();
        break;
      case 5:
        pending_run_edge_ = kNone;
        run_level_ = pending_run_high_;
        break;
      case 6:
        pending_reset_rise_ = kNone;
        break;
      case 7:
        pending_reset_fall_ = kNone;
        break;
      default:
        clocks_[best_src].pop();
        break;
    }

    if (e.t_us >= t0_us) {
      out[n++] = e;
    }
  }
  return n;
}

}  // namespace neon
