#include "neon/config/model.hpp"

#include <cstring>

namespace neon {

namespace {

struct BlobHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t payload_size;
  uint32_t crc;
};
static_assert(sizeof(BlobHeader) == 12, "packed header expected");

template <typename T>
void clamp(T* v, T lo, T hi) {
  if (*v < lo) *v = lo;
  if (*v > hi) *v = hi;
}

}  // namespace

uint32_t crc32(const uint8_t* data, size_t len) {
  uint32_t crc = 0xffffffffu;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int b = 0; b < 8; ++b) {
      crc = (crc >> 1) ^ (0xedb88320u & (~(crc & 1u) + 1u));
    }
  }
  return ~crc;
}

void config_sanitize(Config* cfg) {
  for (auto& c : cfg->engine.clocks) {
    clamp<uint32_t>(&c.ppqn, 1, 192);
    clamp<uint32_t>(&c.mult, 1, 16);
    clamp<uint32_t>(&c.div, 1, 16);
    clamp<uint32_t>(&c.trig_len_us, 100, 100000);
    clamp<uint8_t>(&c.duty_pct, 1, 99);
    clamp<uint8_t>(&c.shuffle_pct, 0, 75);
    if (c.mode != ClockOutputConfig::PulseMode::kSquare) {
      c.mode = ClockOutputConfig::PulseMode::kTrigger;
    }
  }
  if (cfg->engine.reset_mode != ResetMode::kEveryBar &&
      cfg->engine.reset_mode != ResetMode::kOff) {
    cfg->engine.reset_mode = ResetMode::kStartOfPlay;
  }
  clamp<uint32_t>(&cfg->engine.reset_trig_len_us, 100, 100000);
  clamp<int32_t>(&cfg->engine.latency_us, -50000, 50000);
  clamp<uint16_t>(&cfg->tempo_cv_min_bpm, 1, 998);
  clamp<uint16_t>(&cfg->tempo_cv_max_bpm, 2, 999);
  if (cfg->tempo_cv_max_bpm <= cfg->tempo_cv_min_bpm) {
    cfg->tempo_cv_max_bpm = cfg->tempo_cv_min_bpm + 1;
  }
  clamp<uint32_t>(&cfg->quantum_beats, 1, 16);
  if (cfg->clock_source != ClockSource::kLinkMaster &&
      cfg->clock_source != ClockSource::kExternalMaster) {
    cfg->clock_source = ClockSource::kAuto;
  }
  clamp<uint32_t>(&cfg->clock_in_ppqn, 1, 96);

  cfg->ble_enabled = cfg->ble_enabled ? 1 : 0;
  cfg->midi_clock_out = cfg->midi_clock_out ? 1 : 0;
  MidiRouteConfig& m = cfg->midi;
  if (m.midi_channel > 15) {
    m.midi_channel = 255;
  }
  if (m.gate_target > MidiRouteConfig::kTargetRun) {
    m.gate_target = MidiRouteConfig::kTargetNone;
  }
  if (m.cc_latency > 127) {
    m.cc_latency = MidiRouteConfig::kCcOff;
  }
  if (m.cc_shuffle_base > 124) {
    m.cc_shuffle_base = MidiRouteConfig::kCcOff;
  }
  if (m.clock_policy != MidiRouteConfig::ClockPolicy::kReplace &&
      m.clock_policy != MidiRouteConfig::ClockPolicy::kMerge) {
    m.clock_policy = MidiRouteConfig::ClockPolicy::kIgnore;
  }
}

size_t config_blob_size() { return sizeof(BlobHeader) + sizeof(Config); }

size_t config_encode(const Config& cfg, uint8_t* buf, size_t cap) {
  if (cap < config_blob_size()) {
    return 0;
  }
  BlobHeader h;
  h.magic = kConfigMagic;
  h.version = kConfigVersion;
  h.payload_size = static_cast<uint16_t>(sizeof(Config));
  std::memcpy(buf + sizeof(BlobHeader), &cfg, sizeof(Config));
  h.crc = crc32(buf + sizeof(BlobHeader), sizeof(Config));
  std::memcpy(buf, &h, sizeof(BlobHeader));
  return config_blob_size();
}

bool config_decode(const uint8_t* buf, size_t len, Config* out) {
  if (len < sizeof(BlobHeader)) {
    return false;
  }
  BlobHeader h;
  std::memcpy(&h, buf, sizeof(BlobHeader));
  if (h.magic != kConfigMagic || h.version != kConfigVersion ||
      h.payload_size != sizeof(Config) ||
      len < sizeof(BlobHeader) + h.payload_size) {
    return false;
  }
  if (crc32(buf + sizeof(BlobHeader), h.payload_size) != h.crc) {
    return false;
  }
  std::memcpy(out, buf + sizeof(BlobHeader), sizeof(Config));
  config_sanitize(out);
  return true;
}

}  // namespace neon
