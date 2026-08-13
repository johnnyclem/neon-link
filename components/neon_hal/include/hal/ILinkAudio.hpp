#pragma once

// The Link Audio seam (Link 4.0's LinkAudioSink / LinkAudioSource).
//
// Beats cross this boundary as Q32.32 int64, never as doubles: the ESP
// wrapper on core 0 is the only place that converts, so the audio task on
// core 1 stays double-free. Nothing here touches the network — the RT
// methods push and pop lock-free rings that a core-0 pump task drains.

#include <cstddef>
#include <cstdint>

namespace hal {

// A channel offered by a peer (or by us), as Link Audio discovery reports
// it. `id` is what subscribe() takes; `name` is what a person reads.
struct AudioChannelInfo {
  char id[48] = {};
  char name[32] = {};
  uint32_t sample_rate = 0;
  uint8_t num_channels = 0;
  uint8_t is_local = 0;   // one of ours, so the UI can grey it out
  uint8_t pad_[2] = {};
};

class ILinkAudio {
 public:
  virtual ~ILinkAudio() = default;

  // Master mix tap + line-in tap.
  static constexpr int kMaxSinks = 2;

  // ---- Control plane. Core 0 only. ----

  // Publishes a named channel. Returns a sink handle, or -1. Link only
  // transmits while at least one peer is subscribed.
  virtual int sink_create(const char* name, uint32_t rate, uint8_t channels,
                          uint32_t max_block_frames) = 0;
  virtual void sink_destroy(int sink) = 0;
  virtual bool sink_has_subscribers(int sink) const = 0;

  // Channels visible on the session right now. Returns the number written.
  virtual size_t channels(AudioChannelInfo* out, size_t cap) = 0;

  // One active subscription at a time (the module has one receive path).
  virtual bool subscribe(const char* channel_id) = 0;
  virtual void unsubscribe() = 0;
  virtual bool subscribed() const = 0;

  // Moves queued audio between the rings and the network. Called by the
  // pump task on core 0; never from the audio task.
  virtual void pump() = 0;

  // ---- RT plane. Audio task only; lock-free. ----

  virtual void sink_write(int sink, const int16_t* interleaved,
                          uint32_t frames, int64_t begin_beat_q32,
                          int64_t end_beat_q32) = 0;

  // Pops one received block. Returns the frame count (0 when nothing is
  // queued) and fills in the format and beat window it arrived with.
  virtual uint32_t source_read(int16_t* interleaved, uint32_t max_frames,
                               uint32_t& sample_rate, uint8_t& channels,
                               int64_t& begin_beat_q32,
                               int64_t& end_beat_q32) = 0;

  virtual uint32_t source_dropped() const = 0;
  virtual uint32_t sink_dropped() const = 0;

  // Peers listening to anything we publish.
  virtual uint32_t subscriber_count() const = 0;

  // False when the build has no Link Audio behind it (the stub, or a Link
  // that predates 4.0). The UI uses this to explain itself.
  virtual bool available() const = 0;
};

}  // namespace hal
