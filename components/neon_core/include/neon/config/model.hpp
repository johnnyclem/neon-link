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

// One stored WiFi station credential. The module walks the list in order
// on boot, attempting each network wifi_retries times before moving on.
struct WifiNetwork {
  char ssid[33] = {};
  char pass[65] = {};
  uint8_t hidden = 0;  // network does not broadcast its SSID
  uint8_t pad_[3] = {};
};

inline constexpr int kWifiSlots = 4;

// When the module creates its own network.
enum class ApPolicy : uint8_t {
  kFallback = 0,  // only when no stored network can be joined
  kAlways = 1,    // never join a stored network; always self-host
  kOff = 2,       // never create an access point
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

  // Ableton Link 3 start/stop sync: follow (and broadcast) transport
  // changes from other peers. Off means the local transport is private.
  uint8_t start_stop_sync = 1;

  // Separate from engine.latency_us: MIDI gear has its own input latency,
  // so the TRS/BLE clock stream can be nudged independently of the CV
  // outputs. Range mirrors the legacy editors' -10..+100 ms, widened.
  int32_t midi_nudge_us = 0;

  // Tempo used when no Link peer dictates one (tap tempo, +/- nudge, and
  // the editor's tempo field all land here so it survives a reboot).
  uint32_t tempo_milli_bpm = 120000;

  MidiRouteConfig midi;

  // Identity: drives the mDNS hostname (<name>.local), the default access
  // point SSID, and the editor's title.
  char device_name[24] = "neon-link";

  // Display / indicator brightness, 0..255 (0 blanks the panel).
  uint8_t display_brightness = 255;

  // WiFi station list. Slot 0 is what the single-network editor and the
  // menuconfig defaults write.
  WifiNetwork wifi[kWifiSlots];
  uint8_t wifi_retries = 3;  // attempts per network before moving on

  // Access point.
  ApPolicy ap_policy = ApPolicy::kFallback;
  uint8_t ap_require_pass = 0;  // 0 = open network (easy Link jams)
  uint8_t ap_hidden = 0;
  uint8_t ap_channel = 1;
  char ap_ssid[33] = {};  // empty = derive from device_name + MAC
  char ap_pass[65] = "link1234";

  // Full-screen beat number (1, 2, 3, 4…) while the transport is running.
  // Appended in v3 so a v2 NVS blob still decodes (new field keeps default).
  // On by default: 1 and 3 are white on black, 2 and 4 are black on white.
  uint8_t big_beat_display = 1;
};

inline constexpr uint32_t kConfigMagic = 0x4e4c4346;  // "NLCF"
inline constexpr uint16_t kConfigVersion = 3;

// Tempo limits shared by the tap estimator, the editor, and the encoder.
inline constexpr uint32_t kMinMilliBpm = 20000;
inline constexpr uint32_t kMaxMilliBpm = 999000;

// Reduce an arbitrary user-entered name to a DNS-safe label (lowercase
// alphanumerics and single hyphens, no leading/trailing hyphen). Falls
// back to "neon-link" when nothing usable survives. Returns the length
// written; `out` is always NUL-terminated.
size_t sanitize_hostname(const char* in, char* out, size_t cap);

// The SSID the module advertises in access point mode: the explicit
// ap_ssid when set, otherwise "<DEVICE-NAME>-XXXX" from the MAC.
size_t ap_ssid_for(const Config& cfg, const uint8_t mac[6], char* out,
                   size_t cap);

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
