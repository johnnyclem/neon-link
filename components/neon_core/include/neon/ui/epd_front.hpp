#pragma once

#include <cstdint>

#include "neon/config/model.hpp"

namespace neon {

// CrowPanel 5.79" front-panel state machine. Pure logic, host-tested.
// The e-paper task maps physical keys onto these calls; this type never
// talks to GPIO or NVS.
//
// Live: up/down nudge BPM, confirm toggles transport, cancel is idle,
// cancel-long opens the power popup, chord (both buttons) opens the
// inverted settings list. Idle 25 s or a second chord returns to live.
class EpdFrontPanel {
 public:
  enum class Mode : uint8_t { kSplash, kLive, kMenu, kEdit, kPower };
  enum class Action : uint8_t { kNone, kReboot, kPowerOff };

  static constexpr int kItems = 6;
  static constexpr int64_t kIdleUs = 25000000;
  static constexpr int kPowerChoices = 3;  // Restart, Power Off, Cancel

  explicit EpdFrontPanel(Config* cfg);

  void on_up(int64_t now_us);
  void on_down(int64_t now_us);
  void on_confirm(int64_t now_us);
  void on_cancel(int64_t now_us);
  void on_chord(int64_t now_us);
  void on_cancel_long(int64_t now_us);
  void tick(int64_t now_us);

  Mode mode() const { return mode_; }
  bool invert() const { return mode_ == Mode::kMenu || mode_ == Mode::kEdit; }
  int cursor() const { return cursor_; }
  int power_cursor() const { return power_cur_; }

  bool take_dirty();
  Action take_action();
  int take_nudge();
  bool take_toggle();

  const char* item_label(int index) const;
  void item_value(int index, char* buf, int cap) const;
  static const char* power_label(int index);

 private:
  bool readonly(int index) const;
  void touch(int64_t now_us);
  void step(int delta);
  void stash();
  void revert();

  Config* cfg_;
  Mode mode_ = Mode::kSplash;
  int cursor_ = 0;
  int power_cur_ = 2;
  bool dirty_ = false;
  Action action_ = Action::kNone;
  int nudge_ = 0;
  bool toggle_ = false;
  int64_t last_us_ = 0;
  uint32_t stash32_ = 0;
  uint8_t stash8_ = 0;
};

}  // namespace neon
