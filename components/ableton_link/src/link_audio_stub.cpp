// The no-op Link Audio implementation.
//
// Compiled whenever the build has no ableton::LinkAudio behind it: the
// CONFIG_NEON_LINK_STUB leg, and any build where CONFIG_NEON_LINK_AUDIO is
// off (a Link submodule older than 4.0). Everything upstream — the audio
// task, the sinks, the REST surface, the Audio page — keeps compiling and
// running; available() returns false and the UI explains that streaming is
// not in this firmware rather than silently doing nothing.

#include "ablink/audio.hpp"

namespace ablink {

namespace {

class NoLinkAudio : public hal::ILinkAudio {
 public:
  int sink_create(const char*, uint32_t, uint8_t, uint32_t) override {
    return -1;
  }
  void sink_destroy(int) override {}
  bool sink_has_subscribers(int) const override { return false; }
  size_t channels(hal::AudioChannelInfo*, size_t) override { return 0; }
  bool subscribe(const char*) override { return false; }
  void unsubscribe() override {}
  bool subscribed() const override { return false; }
  void pump() override {}
  void set_enabled(bool) override {}
  void set_peer_name(const char*) override {}
  void set_quantum(double) override {}

  void sink_write(int, const int16_t*, uint32_t, int64_t, int64_t) override {}
  uint32_t source_read(int16_t*, uint32_t, uint32_t&, uint8_t&, int64_t&,
                       int64_t&) override {
    return 0;
  }
  uint32_t source_dropped() const override { return 0; }
  uint32_t sink_dropped() const override { return 0; }
  uint32_t subscriber_count() const override { return 0; }
  bool available() const override { return false; }
};

}  // namespace

hal::ILinkAudio& link_audio() {
  static NoLinkAudio instance;
  return instance;
}

void link_audio_start_pump() {}

}  // namespace ablink
