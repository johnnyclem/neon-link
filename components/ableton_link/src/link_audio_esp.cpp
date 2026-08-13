// Link Audio (Link 4.0) on ESP-IDF.
//
// Shape of the thing:
//
//   audio task (core 1) --sink_write--> [tx ring] --+
//                                                    |  pump task (core 0)
//   audio task (core 1) <--source_read-- [rx ring] <-+
//                                             ^
//                          LinkAudioSource callback (Link's network thread)
//
// Nothing here is called from the audio task except sink_write() and
// source_read(), and both of those only touch lock-free rings. lwIP never
// runs on core 1, and network jitter never stalls a render block.
//
// Beats cross the ILinkAudio boundary as Q32.32. The double↔Q32.32
// conversions live in this file and nowhere else.
//
// The constructors and buffer handles here match the real Link-4.0 API
// (LinkAudio(bpm, name), LinkAudioSink(link, name, maxSamples),
// LinkAudioSource(link, ChannelId, callback)) — not the guessed surface
// in the original design spec.

#include <ableton/LinkAudio.hpp>

#include <cstdio>
#include <cstring>
#include <new>

#include "ablink/audio.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "neon/audio/frame_ring.hpp"

namespace ablink {

using LinkImpl = ableton::LinkAudio;

namespace detail {
LinkImpl* link_instance();
}

namespace {

const char* kTag = "link_audio";

constexpr uint32_t kMaxBlockFrames = 512;
constexpr uint32_t kRxSlots = 16;
constexpr uint32_t kTxSlots = 8;
constexpr uint32_t kPumpPeriodMs = 4;

int64_t beats_to_q32(double beats) {
  return static_cast<int64_t>(beats * 4294967296.0);
}

double q32_to_beats(int64_t q32) {
  return static_cast<double>(q32) / 4294967296.0;
}

void id_to_hex(const ableton::ChannelId& id, char out[17]) {
  static const char kHex[] = "0123456789abcdef";
  for (int i = 0; i < 8; ++i) {
    out[i * 2] = kHex[(id[i] >> 4) & 0xf];
    out[i * 2 + 1] = kHex[id[i] & 0xf];
  }
  out[16] = '\0';
}

bool hex_to_id(const char* hex, ableton::ChannelId* out) {
  if (hex == nullptr || std::strlen(hex) != 16 || out == nullptr) {
    return false;
  }
  ableton::ChannelId id{};
  for (int i = 0; i < 8; ++i) {
    unsigned v = 0;
    if (std::sscanf(hex + i * 2, "%2x", &v) != 1) {
      return false;
    }
    id[i] = static_cast<uint8_t>(v);
  }
  *out = id;
  return true;
}

void* psram_alloc(size_t bytes) {
  void* p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (p == nullptr) {
    p = heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
  }
  return p;
}

struct Sink {
  ableton::LinkAudioSink* sink = nullptr;
  neon::AudioBlockRing ring;
  int16_t* samples = nullptr;
  neon::AudioBlockInfo* infos = nullptr;
  uint32_t rate = 48000;
  uint8_t channels = 2;
  bool active = false;
  bool had_subscribers = false;
};

class LinkAudioEsp final : public hal::ILinkAudio {
 public:
  bool available() const override { return true; }

  void set_enabled(bool enable) override {
    LinkImpl* link = detail::link_instance();
    if (link == nullptr) {
      return;
    }
    link->enableLinkAudio(enable);
  }

  void set_peer_name(const char* name) override {
    LinkImpl* link = detail::link_instance();
    if (link == nullptr || name == nullptr || name[0] == '\0') {
      return;
    }
    link->setPeerName(name);
    std::snprintf(peer_name_, sizeof(peer_name_), "%s", name);
  }

  void set_quantum(double beats) override {
    if (beats >= 1.0 && beats <= 16.0) {
      quantum_ = beats;
    }
  }

  int sink_create(const char* name, uint32_t rate, uint8_t channels,
                  uint32_t max_block_frames) override {
    LinkImpl* link = detail::link_instance();
    if (link == nullptr || name == nullptr || max_block_frames == 0 ||
        max_block_frames > kMaxBlockFrames) {
      return -1;
    }
    for (int i = 0; i < kMaxSinks; ++i) {
      if (sinks_[i].active) {
        continue;
      }
      Sink& s = sinks_[i];
      if (!ensure_sink_storage(s, channels)) {
        return -1;
      }
      const size_t max_samples =
          static_cast<size_t>(max_block_frames) * (channels != 0 ? channels : 2);
      s.sink = new (std::nothrow) ableton::LinkAudioSink(*link, name, max_samples);
      if (s.sink == nullptr) {
        return -1;
      }
      s.rate = rate;
      s.channels = channels != 0 ? channels : 2;
      s.active = true;
      s.had_subscribers = false;
      ESP_LOGI(kTag, "publishing \"%s\" (%lu Hz, %u ch)", name,
               static_cast<unsigned long>(rate),
               static_cast<unsigned>(s.channels));
      return i;
    }
    return -1;
  }

  void sink_destroy(int sink) override {
    if (sink < 0 || sink >= kMaxSinks || !sinks_[sink].active) {
      return;
    }
    Sink& s = sinks_[sink];
    s.active = false;
    s.had_subscribers = false;
    vTaskDelay(pdMS_TO_TICKS(10));
    delete s.sink;
    s.sink = nullptr;
    s.ring.reset();
  }

  bool sink_has_subscribers(int sink) const override {
    if (sink < 0 || sink >= kMaxSinks || !sinks_[sink].active) {
      return false;
    }
    return sinks_[sink].had_subscribers;
  }

  size_t channels(hal::AudioChannelInfo* out, size_t cap) override {
    LinkImpl* link = detail::link_instance();
    if (link == nullptr || out == nullptr || cap == 0) {
      return 0;
    }
    size_t n = 0;
    for (const auto& ch : link->channels()) {
      if (n >= cap) {
        break;
      }
      hal::AudioChannelInfo& info = out[n];
      id_to_hex(ch.id, info.id);
      std::snprintf(info.name, sizeof(info.name), "%s", ch.name.c_str());
      std::snprintf(info.peer, sizeof(info.peer), "%s", ch.peerName.c_str());
      info.sample_rate = 0;
      info.num_channels = 0;
      info.is_local =
          (peer_name_[0] != '\0' && ch.peerName == peer_name_) ? 1 : 0;
      ++n;
    }
    return n;
  }

  bool subscribe(const char* channel_id) override {
    LinkImpl* link = detail::link_instance();
    if (link == nullptr || channel_id == nullptr || channel_id[0] == '\0') {
      return false;
    }
    ableton::ChannelId id{};
    if (!hex_to_id(channel_id, &id)) {
      ESP_LOGW(kTag, "subscribe: \"%s\" is not a 16-char channel id",
               channel_id);
      return false;
    }
    unsubscribe();
    if (!ensure_rx_storage()) {
      return false;
    }
    rx_ring_.reset();
    source_ = new (std::nothrow) ableton::LinkAudioSource(
        *link, id, [this](ableton::LinkAudioSource::BufferHandle handle) {
          on_receive(handle);
        });
    if (source_ == nullptr) {
      return false;
    }
    std::snprintf(sub_id_, sizeof(sub_id_), "%s", channel_id);
    ESP_LOGI(kTag, "subscribed to \"%s\"", sub_id_);
    return true;
  }

  void unsubscribe() override {
    if (source_ == nullptr) {
      return;
    }
    ableton::LinkAudioSource* s = source_;
    source_ = nullptr;
    delete s;
    sub_id_[0] = '\0';
    rx_ring_.reset();
  }

  bool subscribed() const override { return source_ != nullptr; }

  void pump() override {
    LinkImpl* link = detail::link_instance();
    if (link == nullptr) {
      return;
    }
    int16_t block[kMaxBlockFrames * 2];
    const auto state = link->captureAppSessionState();
    for (int i = 0; i < kMaxSinks; ++i) {
      Sink& s = sinks_[i];
      if (!s.active || s.sink == nullptr) {
        continue;
      }
      neon::AudioBlockInfo info;
      bool saw_sub = false;
      while (s.ring.pop(&info, block, sizeof(block) / sizeof(block[0]))) {
        ableton::LinkAudioSink::BufferHandle handle(*s.sink);
        if (!handle) {
          continue;  // no subscriber; drain so the ring cannot back up
        }
        saw_sub = true;
        const size_t samples =
            static_cast<size_t>(info.frames) * s.channels;
        const size_t n =
            samples < handle.maxNumSamples ? samples : handle.maxNumSamples;
        std::memcpy(handle.samples, block, n * sizeof(int16_t));
        handle.commit(state, q32_to_beats(info.begin_beat_q32), quantum_,
                      info.frames, s.channels, info.sample_rate);
      }
      s.had_subscribers = saw_sub;
    }
  }

  void sink_write(int sink, const int16_t* interleaved, uint32_t frames,
                  int64_t begin_beat_q32, int64_t end_beat_q32) override {
    if (sink < 0 || sink >= kMaxSinks || !sinks_[sink].active ||
        interleaved == nullptr) {
      return;
    }
    Sink& s = sinks_[sink];
    neon::AudioBlockInfo info;
    info.frames = frames;
    info.sample_rate = s.rate;
    info.channels = s.channels;
    info.begin_beat_q32 = begin_beat_q32;
    info.end_beat_q32 = end_beat_q32;
    if (!s.ring.push(info, interleaved)) {
      ++sink_dropped_;
    }
  }

  uint32_t source_read(int16_t* interleaved, uint32_t max_frames,
                       uint32_t& sample_rate, uint8_t& channels,
                       int64_t& begin_beat_q32,
                       int64_t& end_beat_q32) override {
    neon::AudioBlockInfo info;
    if (!rx_ring_.pop(&info, interleaved, max_frames * 2)) {
      return 0;
    }
    sample_rate = info.sample_rate;
    channels = info.channels;
    begin_beat_q32 = info.begin_beat_q32;
    end_beat_q32 = info.end_beat_q32;
    return info.frames;
  }

  uint32_t source_dropped() const override { return rx_ring_.dropped(); }
  uint32_t sink_dropped() const override { return sink_dropped_; }

  uint32_t subscriber_count() const override {
    uint32_t n = 0;
    for (int i = 0; i < kMaxSinks; ++i) {
      if (sinks_[i].active && sinks_[i].had_subscribers) {
        ++n;
      }
    }
    return n;
  }

 private:
  void on_receive(const ableton::LinkAudioSource::BufferHandle& handle) {
    if (source_ == nullptr || handle.samples == nullptr) {
      return;
    }
    neon::AudioBlockInfo out;
    out.frames = static_cast<uint32_t>(handle.info.numFrames);
    out.sample_rate = handle.info.sampleRate;
    out.channels = static_cast<uint8_t>(handle.info.numChannels);
    LinkImpl* link = detail::link_instance();
    if (link != nullptr) {
      const auto state = link->captureAppSessionState();
      if (const auto begin = handle.info.beginBeats(state, quantum_)) {
        out.begin_beat_q32 = beats_to_q32(*begin);
      }
      if (const auto end = handle.info.endBeats(state, quantum_)) {
        out.end_beat_q32 = beats_to_q32(*end);
      }
    }
    rx_ring_.push(out, handle.samples);
  }

  bool ensure_sink_storage(Sink& s, uint8_t channels) {
    if (s.samples != nullptr) {
      return true;
    }
    const size_t samples = static_cast<size_t>(kTxSlots) * kMaxBlockFrames * 2;
    s.samples = static_cast<int16_t*>(psram_alloc(samples * sizeof(int16_t)));
    s.infos = static_cast<neon::AudioBlockInfo*>(
        psram_alloc(kTxSlots * sizeof(neon::AudioBlockInfo)));
    if (s.samples == nullptr || s.infos == nullptr) {
      ESP_LOGE(kTag, "out of memory for the publish ring");
      return false;
    }
    s.ring.init(s.samples, s.infos, kTxSlots, kMaxBlockFrames,
                channels != 0 ? channels : 2);
    return true;
  }

  bool ensure_rx_storage() {
    if (rx_samples_ != nullptr) {
      return true;
    }
    const size_t samples = static_cast<size_t>(kRxSlots) * kMaxBlockFrames * 2;
    rx_samples_ = static_cast<int16_t*>(psram_alloc(samples * sizeof(int16_t)));
    rx_infos_ = static_cast<neon::AudioBlockInfo*>(
        psram_alloc(kRxSlots * sizeof(neon::AudioBlockInfo)));
    if (rx_samples_ == nullptr || rx_infos_ == nullptr) {
      ESP_LOGE(kTag, "out of memory for the receive ring");
      return false;
    }
    rx_ring_.init(rx_samples_, rx_infos_, kRxSlots, kMaxBlockFrames, 2);
    return true;
  }

  Sink sinks_[kMaxSinks];
  ableton::LinkAudioSource* source_ = nullptr;
  char sub_id_[48] = {};
  char peer_name_[32] = {};
  double quantum_ = 4.0;

  neon::AudioBlockRing rx_ring_;
  int16_t* rx_samples_ = nullptr;
  neon::AudioBlockInfo* rx_infos_ = nullptr;
  uint32_t sink_dropped_ = 0;
};

LinkAudioEsp g_link_audio;
TaskHandle_t g_pump_task = nullptr;

void pump_task(void*) {
  for (;;) {
    g_link_audio.pump();
    vTaskDelay(pdMS_TO_TICKS(kPumpPeriodMs));
  }
}

}  // namespace

hal::ILinkAudio& link_audio() { return g_link_audio; }

void link_audio_start_pump() {
  if (g_pump_task != nullptr) {
    return;
  }
  xTaskCreatePinnedToCore(pump_task, "linkaudio", 4096, nullptr, 11,
                          &g_pump_task, 0);
}

}  // namespace ablink
