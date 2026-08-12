#pragma once

#include <cstdint>

#include "neon/config/model.hpp"

namespace neon {

// Live status displayed by the UI (assembled on core 0 from the timeline
// snapshot and network state).
struct UiStatus {
  uint32_t milli_bpm = 120000;
  uint32_t peers = 0;
  bool playing = false;
  uint8_t active_net = 0;  // 0 none, 1 ethernet, 2 wifi
  bool ext_clock = false;
  // Phase within the bar, milli-beats 0..quantum*1000 (for the phase bar
  // and beat LED).
  uint32_t phase_milli_beats = 0;
  uint32_t quantum_beats = 4;
  // Best editor IPv4 ("192.168.x.x") or empty; setup_ap when open AP is up.
  char ip[16] = {};
  bool setup_ap = false;
  // False until the first Link sync, so the hero readout can show its
  // placeholder instead of a misleading tempo.
  bool tempo_valid = true;
  bool ble_on = false;
  // Full-screen 1/2/3/4 while playing. Default on, matching Config.
  bool big_beat_display = true;
};

// Encoder-driven menu state machine (pure logic; host-tested).
//
// The tree is deliberately shallow (DESIGN_SYSTEM.md §11): one menu with
// five destinations, at most one level below it, and long-press as the
// universal way back.
//
//   Home --click--> Menu [Live, Outputs, Network, MIDI, System]
//     Outputs [CLK1..4] --click--> OutputEdit (param list)
//     Network              read-only; credentials are the web UI's job
//     MIDI, System         param lists
//     System > REBOOT   --click--> Confirm
//
// Interaction (§11): rotate moves focus or changes the focused value,
// short press enters/confirms/toggles, long press cancels an edit or goes
// back one level.
//
// The model mutates a Config in place; take_dirty() reports one-shot when
// a value changed so the owner can apply + persist (debounced).
class MenuModel {
 public:
  enum class Screen : uint8_t {
    kHome,
    kMenu,
    kOutputs,
    kOutputEdit,
    kNetwork,
    kMidi,
    kSystem,
    kConfirm,
  };

  // Requests the model cannot carry out itself. The owner polls
  // take_action() and performs the platform-specific part.
  enum class Action : uint8_t {
    kNone,
    kReboot,
  };

  static constexpr int kMenuItems = 5;     // Live, Outputs, Network, MIDI, System
  static constexpr int kOutputsItems = 4;  // CLK1..4
  // ENABLED, PPQN, MULT, DIV, MODE, TRIG MS, DUTY, SHUF, then the parity
  // set: ROLE, FREE RUN, RHYTHM, STEPS, FILLS, ROT, CHANCE, JITTER,
  // PER LOOP. The shape parameters stay first because they are what a
  // user reaches for at the rack; the pattern editor lives in the web UI.
  static constexpr int kOutputEditItems = 17;
  static constexpr int kMidiItems = 5;
  // LATENCY, RESET, SOURCE, IN PPQN, GATE CLK, QUANTUM, RST EDGE,
  // MIDI NDG, SS SYNC, BRIGHT, BEAT, REBOOT — REBOOT stays last.
  static constexpr int kSystemItems = 12;
  static constexpr int kSystemRebootItem = kSystemItems - 1;

  explicit MenuModel(Config* cfg) : cfg_(cfg) {}

  void on_rotate(int detents);
  void on_click();
  // Cancels an in-progress edit if there is one, otherwise goes back one
  // level. From the menu this returns to the live screen.
  void on_long_press();

  bool take_dirty();
  Action take_action();

  Screen screen() const { return screen_; }
  int cursor() const { return cursor_; }
  bool editing() const { return editing_; }
  int output_index() const { return output_; }
  bool confirm_yes() const { return confirm_yes_; }
  int item_count() const;

  // Device title for the active screen, from the shared vocabulary.
  const char* screen_title() const;

  // Label and current value string for a list row on the active screen.
  const char* item_label(int index) const;
  void item_value(int index, char* buf, int cap) const;

 private:
  void adjust_output_param(int index, int delta);
  void adjust_midi(int index, int delta);
  void adjust_system(int index, int delta);
  void mark_dirty() { dirty_ = true; }

  Config* cfg_;
  Screen screen_ = Screen::kHome;
  int cursor_ = 0;
  int output_ = 0;
  bool editing_ = false;
  bool dirty_ = false;
  bool confirm_yes_ = false;
  Action action_ = Action::kNone;
};

}  // namespace neon
