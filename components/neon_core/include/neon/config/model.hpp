#pragma once

#include <cstddef>
#include <cstdint>

#include "neon/midi/router.hpp"
#include "neon/multi_engine.hpp"
#include "neon/output_config.hpp"

namespace neon {

// The single persisted configuration struct (one NVS blob). Fixed-width
// members only; encode/decode add a header with magic, version, size, and
// CRC so a corrupt or foreign blob falls back to defaults. Extend by
// appending fields and bumping kConfigVersion (decode rejects unknown
// versions and the caller uses defaults — explicit migrations can be
// added when there is a fleet to migrate).
enum class ClockSource : uint8_t {
  kAuto = 0,            // external when CLK IN is active, Link otherwise
  kLinkMaster = 1,      // ignore CLK IN
  kExternalMaster = 2,  // follow CLK IN whenever it is active
};

struct Config {
  EngineConfig engine;

  // Tempo CV mapping: BPM range that spans the 0..5 V output.
  uint16_t tempo_cv_min_bpm = 20;
  uint16_t tempo_cv_max_bpm = 300;

  uint32_t quantum_beats = 4;

  // Bidirectional operation (milestone 5).
  ClockSource clock_source = ClockSource::kAuto;
  uint32_t clock_in_ppqn = 4;  // pulses per beat expected on CLK IN

  // BLE MIDI (milestone 7). ble_enabled=0 keeps the BT controller fully
  // deinitialized for maximum Link reliability (the SOFTWARE.md kill
  // switch).
  uint8_t ble_enabled = 1;
  uint8_t midi_clock_out = 1;  // 24 PPQN Link-derived clock on TRS
  MidiRouteConfig midi;

  // WiFi station credentials (milestone 8; empty = fall back to the
  // menuconfig defaults). Changes apply on the next boot.
  char wifi_ssid[33] = {};
  char wifi_pass[65] = {};
};

inline constexpr uint32_t kConfigMagic = 0x4e4c4346;  // "NLCF"
inline constexpr uint16_t kConfigVersion = 1;

// Wire size of an encoded config blob.
size_t config_blob_size();

// Serialize with header + CRC. `cap` must be >= config_blob_size().
// Returns bytes written, or 0 on insufficient capacity.
size_t config_encode(const Config& cfg, uint8_t* buf, size_t cap);

// Validate header, version, and CRC; on success fills `out` (clamped to
// valid ranges). Returns false on any mismatch — caller keeps defaults.
bool config_decode(const uint8_t* buf, size_t len, Config* out);

// Clamp all fields into their valid ranges (also applied by decode).
void config_sanitize(Config* cfg);

// CRC-32 (IEEE, reflected), for the config blob and later preset slots.
uint32_t crc32(const uint8_t* data, size_t len);

}  // namespace neon
