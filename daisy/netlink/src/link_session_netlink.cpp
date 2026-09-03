// Real Ableton Link session for the netlink build: the upstream
// header-only Link with the netdaisy platform selected by the shadowed
// ableton/platforms/Config.hpp (lwIP-over-USB sockets, polled timers,
// no asio, no RTOS). link_session_t41.cpp with the session handed to
// the service through the daisy_session() seam instead of
// ablink::session().

#include <ableton/Link.hpp>

#include "neon/transport.hpp"
#include "session_daisy.h"

namespace {

class LinkSessionNetlink final : public hal::ILinkSession {
 public:
  void start(double initial_bpm) override {
    if (link_ == nullptr) {
      // Heap-allocated on first start(), like the ESP32 and Teensy
      // sessions: constructing ableton::Link opens sockets and timers,
      // which must not happen during static initialization.
      link_ = new ableton::Link(initial_bpm);
      link_->enableStartStopSync(start_stop_sync_);
    }
    link_->enable(true);
  }

  bool capture(hal::LinkState& out) override {
    if (link_ == nullptr) {
      return false;
    }
    const auto state = link_->captureAppSessionState();
    const auto now = link_->clock().micros();  // daisy_now_us domain
    out.origin_us = now.count();
    out.tempo_bpm = state.tempo();
    out.beat_at_origin = state.beatAtTime(now, quantum_);
    out.quantum = quantum_;
    out.playing = neon::playing_at(state.isPlaying(),
                                   state.timeForIsPlaying().count(), now.count());
    out.num_peers = static_cast<uint32_t>(link_->numPeers());
    return true;
  }

  void set_tempo(double bpm) override {
    if (link_ == nullptr) {
      return;
    }
    auto state = link_->captureAppSessionState();
    state.setTempo(bpm, link_->clock().micros());
    link_->commitAppSessionState(state);
  }

  void set_playing(bool playing, int64_t at_us) override {
    if (link_ == nullptr) {
      return;
    }
    const auto t = at_us >= 0 ? std::chrono::microseconds(at_us)
                              : link_->clock().micros();
    auto state = link_->captureAppSessionState();
    if (playing) {
      state.setIsPlayingAndRequestBeatAtTime(true, t, 0.0, quantum_);
    } else {
      state.setIsPlaying(false, t);
    }
    link_->commitAppSessionState(state);
  }

  void request_beat_at_time(int64_t t_us) override {
    if (link_ == nullptr) {
      return;
    }
    auto state = link_->captureAppSessionState();
    state.requestBeatAtTime(0.0, std::chrono::microseconds(t_us), quantum_);
    link_->commitAppSessionState(state);
  }

  void set_start_stop_sync(bool enable) override {
    start_stop_sync_ = enable;
    if (link_ != nullptr) {
      link_->enableStartStopSync(enable);
    }
  }

  void set_quantum(double beats) override {
    if (beats >= 1.0 && beats <= 16.0) {
      quantum_ = beats;
    }
  }

 private:
  ableton::Link* link_ = nullptr;
  bool start_stop_sync_ = true;
  double quantum_ = 4.0;
};

}  // namespace

hal::ILinkSession& daisy_session() {
  static LinkSessionNetlink s;
  return s;
}
