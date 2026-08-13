#pragma once

#include <cstdint>
#include <string>

namespace neon::client {

// Partial PUT body. neon::Config is a fully populated struct, so
// "latency_us == 0" cannot mean unset. A field list is the only honest
// representation of a partial document.
class JsonPatch {
 public:
  void setEngineLatency(int32_t us);
  void setMidiNudge(int32_t us);
  void setQuantum(uint32_t beats);
  void setBleEnabled(bool);
  void setTransportGating(bool);
  void setClockEnabled(int index /*0..3*/, bool);
  void setClockShuffle(int index /*0..3*/, uint8_t pct);

  // Free-form object merge for SaveBar drafts (full subtrees the
  // caller built). `json` must be a JSON object; it is merged at the
  // document root.
  void mergeObject(const char* json, size_t len);

  // Compact JSON. Clock fields produce leading {} so config_from_json's
  // index walk hits the right jack. `null` will not skip.
  std::string toJson() const;

  bool empty() const;

 private:
  bool has_latency_ = false;
  int32_t latency_us_ = 0;
  bool has_nudge_ = false;
  int32_t midi_nudge_us_ = 0;
  bool has_quantum_ = false;
  uint32_t quantum_ = 0;
  bool has_ble_ = false;
  bool ble_enabled_ = false;
  bool has_gating_ = false;
  bool transport_gating_ = false;

  struct ClockBits {
    bool has_enabled = false;
    bool enabled = false;
    bool has_shuffle = false;
    uint8_t shuffle_pct = 0;
  };
  ClockBits clocks_[4] = {};
  int clock_hi_ = -1;  // highest index that has a field

  std::string merge_;
};

}  // namespace neon::client
