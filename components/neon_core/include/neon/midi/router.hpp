#pragma once

#include <cstdint>

#include "neon/midi/ble_midi_parser.hpp"

namespace neon {

// BLE-MIDI routing configuration (SOFTWARE.md §4 routing matrix).
struct MidiRouteConfig {
  static constexpr uint8_t kTargetNone = 255;
  static constexpr uint8_t kTargetRun = 4;  // 0..3 = CLK1..4
  static constexpr uint8_t kCcOff = 255;

  uint8_t midi_channel = 255;  // 0..15, 255 = omni
  uint8_t gate_target = kTargetNone;
  bool pitch_cv = false;  // note pitch -> Tempo CV jack, 1 V/oct
  uint8_t cc_latency = kCcOff;       // CC number mapped to latency
  uint8_t cc_shuffle_base = kCcOff;  // base CC: base..base+3 -> CLK1..4 shuffle
  bool transport_enabled = true;
  bool pc_presets = true;  // Program Change n recalls preset slot n % 4

  enum class ClockPolicy : uint8_t {
    kIgnore = 0,   // TRS clock comes from the Link timeline (default)
    kReplace = 1,  // pass BLE clock through; Link-derived clock muted
    kMerge = 2,    // both sources forwarded
  };
  ClockPolicy clock_policy = ClockPolicy::kIgnore;
};

// Actions the router emits; the ESP layer implements them (gate queue to
// the pulse task, LEDC pitch CV, Link transport, TRS UART), host tests
// record them.
class IRouterSink {
 public:
  virtual ~IRouterSink() = default;
  virtual void gate(uint8_t target, bool on) = 0;
  virtual void pitch_cv(uint16_t ratio_q16) = 0;
  virtual void latency_offset(int32_t latency_us) = 0;
  virtual void shuffle(uint8_t clock_index, uint8_t pct) = 0;
  virtual void transport(bool play) = 0;
  virtual void trs_realtime(uint8_t status) = 0;
  virtual void program_change(uint8_t program) = 0;
};

// 1 V/oct pitch mapping: 5 V span = 60 semitones from the base note.
uint16_t pitch_to_cv_q16(uint8_t note, uint8_t base_note = 36);

// Routes parsed BLE-MIDI to module actions. Mono last-note gate: a
// note-on (re)opens the gate; only the off of the sounding note (or
// all-notes-off CC 123) closes it.
class MidiRouter final : public IMidiSink {
 public:
  MidiRouter(const MidiRouteConfig& cfg, IRouterSink* sink)
      : cfg_(cfg), sink_(sink) {}

  void set_config(const MidiRouteConfig& cfg) { cfg_ = cfg; }

  void on_message(const MidiMessage& m) override;
  void on_realtime(uint8_t status) override;

 private:
  bool channel_match(uint8_t status) const;

  MidiRouteConfig cfg_;
  IRouterSink* sink_;
  uint8_t active_note_ = 255;
};

}  // namespace neon
