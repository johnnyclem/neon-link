// Free-running stand-in for the Ableton Link session
// (CONFIG_NEON_LINK_STUB). Keeps a linear beat grid from start() at a
// settable tempo so everything downstream of the timeline snapshot behaves
// exactly as with a real session — just with zero peers.

#include "ablink/session.hpp"
#include "esp_timer.h"

namespace ablink {
namespace {

class LinkStub final : public hal::ILinkSession {
 public:
  void start(double initial_bpm) override {
    if (!started_) {
      tempo_bpm_ = initial_bpm;
      t0_us_ = esp_timer_get_time();
      started_ = true;
    }
  }

  bool capture(hal::LinkState& out) override {
    if (!started_) {
      return false;
    }
    const int64_t now = esp_timer_get_time();
    out.origin_us = now;
    out.tempo_bpm = tempo_bpm_;
    out.beat_at_origin = beat_at(now);
    out.quantum = quantum_;
    out.playing = playing_;
    out.num_peers = 0;
    return true;
  }

  void set_tempo(double bpm) override {
    if (!started_ || bpm <= 0.0) {
      return;
    }
    // Re-anchor so the beat grid stays continuous through the change.
    const int64_t now = esp_timer_get_time();
    beat0_ = beat_at(now);
    t0_us_ = now;
    tempo_bpm_ = bpm;
  }

  void set_playing(bool playing) override { playing_ = playing; }

  void request_beat_at_time(int64_t t_us) override {
    if (!started_) {
      return;
    }
    // Re-anchor the internal grid: beat 0 lands at t_us.
    t0_us_ = t_us;
    beat0_ = 0.0;
  }

  void set_start_stop_sync(bool enable) override { start_stop_sync_ = enable; }

  void set_quantum(double beats) override {
    if (beats >= 1.0 && beats <= 16.0) {
      quantum_ = beats;
    }
  }

 private:
  double beat_at(int64_t t_us) const {
    const double mpb = 60000000.0 / tempo_bpm_;
    return beat0_ + static_cast<double>(t_us - t0_us_) / mpb;
  }

  bool started_ = false;
  bool playing_ = true;
  double tempo_bpm_ = 120.0;
  double beat0_ = 0.0;
  int64_t t0_us_ = 0;
  bool start_stop_sync_ = true;
  double quantum_ = 4.0;
};

LinkStub g_session;

}  // namespace

hal::ILinkSession& session() { return g_session; }

}  // namespace ablink
