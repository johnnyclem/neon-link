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
};

// Encoder-driven menu state machine (pure logic; host-tested).
//
// Home --click--> Menu [Outputs, Settings, Back]
//   Outputs [CLK1..4, Back] --click--> OutputEdit (param list; click
//     toggles edit mode, rotation adjusts the value in edit mode)
//   Settings (param list, same editing pattern)
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
    kSettings,
  };

  static constexpr int kMenuItems = 3;     // Outputs, Settings, Back
  static constexpr int kOutputsItems = 5;  // CLK1..4, Back
  static constexpr int kOutputEditItems = 9;
  static constexpr int kSettingsItems = 6;

  explicit MenuModel(Config* cfg) : cfg_(cfg) {}

  void on_rotate(int detents);
  void on_click();

  bool take_dirty();

  Screen screen() const { return screen_; }
  int cursor() const { return cursor_; }
  bool editing() const { return editing_; }
  int output_index() const { return output_; }
  int item_count() const;

  // Label and current value string for a list row on the active screen.
  const char* item_label(int index) const;
  void item_value(int index, char* buf, int cap) const;

 private:
  void adjust_output_param(int index, int delta);
  void adjust_setting(int index, int delta);
  void mark_dirty() { dirty_ = true; }

  Config* cfg_;
  Screen screen_ = Screen::kHome;
  int cursor_ = 0;
  int output_ = 0;
  bool editing_ = false;
  bool dirty_ = false;
};

}  // namespace neon
