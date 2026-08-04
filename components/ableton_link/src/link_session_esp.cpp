// Real Ableton Link session on ESP-IDF. Mirrors the official
// third_party/link/examples/esp32 integration: header-only Link with its
// bundled standalone asio, platform auto-selected via ESP_PLATFORM, and the
// lwIP interface-name shims the example also provides.

#include <ableton/Link.hpp>

#include "ablink/session.hpp"

// ESP-IDF's lwIP does not provide these; Link's interface scanner links
// against them (same shims as the official esp32 example).
extern "C" unsigned int if_nametoindex(const char* /*ifName*/) { return 0; }
extern "C" char* if_indextoname(unsigned int /*ifIndex*/, char* /*ifName*/) {
  return nullptr;
}

namespace ablink {
namespace {

constexpr double kQuantum = 4.0;

class LinkSessionEsp final : public hal::ILinkSession {
 public:
  void start(double initial_bpm) override {
    if (link_ == nullptr) {
      link_ = new ableton::Link(initial_bpm);
      link_->enableStartStopSync(true);
    }
    link_->enable(true);
  }

  bool capture(hal::LinkState& out) override {
    if (link_ == nullptr) {
      return false;
    }
    const auto state = link_->captureAppSessionState();
    const auto now = link_->clock().micros();  // esp_timer domain
    out.origin_us = now.count();
    out.tempo_bpm = state.tempo();
    out.beat_at_origin = state.beatAtTime(now, kQuantum);
    out.quantum = kQuantum;
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
                                             0.0, kQuantum);
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
    state.requestBeatAtTime(0.0, std::chrono::microseconds(t_us), kQuantum);
    link_->commitAppSessionState(state);
  }

 private:
  // Heap-allocated on first start(): constructing ableton::Link spins up
  // sockets and its asio service task, which must not happen from static
  // initialization order.
  ableton::Link* link_ = nullptr;
};

LinkSessionEsp g_session;

}  // namespace

hal::ILinkSession& session() { return g_session; }

}  // namespace ablink
