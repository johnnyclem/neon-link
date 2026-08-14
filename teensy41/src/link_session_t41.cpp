// Real Ableton Link session on the Teensy 4.1: the upstream header-only
// Link with the teensy41 platform selected by the shadowed
// ableton/platforms/Config.hpp (QNEthernet sockets, polled timers, no
// asio, no RTOS). Mirrors link_session_esp.cpp minus the LinkAudio
// facade — Link Audio streaming is not ported yet.

#include <ableton/Link.hpp>

#include "ablink/session.hpp"

namespace ablink {
namespace {

class LinkSessionT41 final : public hal::ILinkSession {
 public:
  void start(double initial_bpm) override {
    if (link_ == nullptr) {
      // Heap-allocated on first start(), like the ESP32 session:
      // constructing ableton::Link opens sockets and timers, which must
      // not happen during static initialization.
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
    const auto now = link_->clock().micros();  // t41_now_us domain
    out.origin_us = now.count();
    out.tempo_bpm = state.tempo();
    out.beat_at_origin = state.beatAtTime(now, quantum_);
    out.quantum = quantum_;
    out.playing = state.isPlaying();
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

  void set_playing(bool playing) override {
    if (link_ == nullptr) {
      return;
    }
    auto state = link_->captureAppSessionState();
    if (playing) {
      state.setIsPlayingAndRequestBeatAtTime(true, link_->clock().micros(),
                                             0.0, quantum_);
    } else {
      state.setIsPlaying(false, link_->clock().micros());
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

LinkSessionT41 g_session;

}  // namespace

hal::ILinkSession& session() { return g_session; }

}  // namespace ablink
