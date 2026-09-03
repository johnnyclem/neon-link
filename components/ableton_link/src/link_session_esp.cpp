// Real Ableton Link session on ESP-IDF. Mirrors the official
// third_party/link/examples/esp32 integration: header-only Link with its
// bundled standalone asio, platform auto-selected via ESP_PLATFORM, and the
// lwIP interface-name shims the example also provides.

#include "sdkconfig.h"

#include <chrono>
#include <string>

#if CONFIG_NEON_LINK_AUDIO
// Link 4.0: LinkAudio is a superset of Link, so the session behaviour below
// is unchanged and one instance backs both facades (see ablink/audio.hpp).
#include <ableton/LinkAudio.hpp>
#else
#include <ableton/Link.hpp>
#endif

#include "ablink/session.hpp"
#include "neon/transport.hpp"

// ESP-IDF's lwIP does not provide these; Link's interface scanner links
// against them (same shims as the official esp32 example).
extern "C" unsigned int if_nametoindex(const char* /*ifName*/) { return 0; }
extern "C" char* if_indextoname(unsigned int /*ifIndex*/, char* /*ifName*/) {
  return nullptr;
}

namespace ablink {

#if CONFIG_NEON_LINK_AUDIO
using LinkImpl = ableton::LinkAudio;
#else
using LinkImpl = ableton::Link;
#endif

namespace {

class LinkSessionEsp final : public hal::ILinkSession {
 public:
  void start(double initial_bpm) override {
    if (link_ == nullptr) {
#if CONFIG_NEON_LINK_AUDIO
      // LinkAudio(bpm, name). Audio stays off until the audio service
      // turns it on — a clock-only session must not pay the stream tax.
      link_ = new LinkImpl(initial_bpm, peer_name_);
      link_->enableLinkAudio(false);
#else
      link_ = new LinkImpl(initial_bpm);
#endif
      link_->enableStartStopSync(start_stop_sync_);
    }
    link_->enable(true);
  }

  void set_peer_name(const char* name) {
    if (name == nullptr || name[0] == '\0') {
      peer_name_ = "neon-link";
    } else {
      peer_name_ = name;
    }
#if CONFIG_NEON_LINK_AUDIO
    if (link_ != nullptr) {
      link_->setPeerName(peer_name_);
    }
#endif
  }

  bool capture(hal::LinkState& out) override {
    if (link_ == nullptr) {
      return false;
    }
    const auto state = link_->captureAppSessionState();
    const auto now = link_->clock().micros();  // esp_timer domain
    out.origin_us = now.count();
    out.tempo_bpm = state.tempo();
    out.beat_at_origin = state.beatAtTime(now, quantum_);
    out.quantum = quantum_;
    // isPlaying() is the scheduled flag, not "playing right now". A
    // quantized stop sets isPlaying=false at the bar line; until then
    // the transport is still running.
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
  // Heap-allocated on first start(): constructing ableton::Link spins up
  // sockets and its asio service task, which must not happen from static
  // initialization order.
  LinkImpl* link_ = nullptr;
  std::string peer_name_ = "neon-link";
  bool start_stop_sync_ = true;
  double quantum_ = 4.0;

 public:
  // The Link Audio facade needs the same instance — it publishes and
  // subscribes on the session this owns. Null until start().
  LinkImpl* instance() const { return link_; }
};

LinkSessionEsp g_session;

}  // namespace

hal::ILinkSession& session() { return g_session; }

namespace detail {
LinkImpl* link_instance() { return g_session.instance(); }
}  // namespace detail

}  // namespace ablink
