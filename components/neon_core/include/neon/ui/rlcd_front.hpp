#pragma once

#include <cstdint>

#include "neon/config/model.hpp"

namespace neon {

// Waveshare RLCD-4.2 front-panel state machine. Pure logic, host-
// tested. The board has exactly two buttons — the side KEY and BOOT —
// so every mode is driven by four gestures: KEY short/long and BOOT
// short/long. The rlcd task maps debounced GPIO edges onto these
// calls; this type never talks to GPIO or NVS.
//
// Live: KEY short toggles transport, BOOT short taps BPM up by one,
// BOOT long opens the Tempo screen (where BOTH directions live), KEY
// long opens the settings list. Tempo: KEY nudges up, BOOT nudges
// down, either held auto-repeats (accelerating), and it self-closes
// after a few idle seconds. Menu: BOOT short/long moves the cursor
// down/up, KEY short activates (the last row opens the power popup),
// KEY long returns to live. Edit: BOOT steps the value, KEY short
// commits, KEY long reverts. Idle returns to live; the boot splash
// clears itself after a few seconds.
//
// Why a Tempo screen rather than BOOT-tap-up / BOOT-hold-down: the
// board's three top-edge buttons are KEY, BOOT, and PWR, but only KEY
// and BOOT are user-readable GPIOs (PWR is the power on/off button).
// With just two control buttons, a single button cannot host both
// directions discoverably. A brief hold surfaces a screen where each
// button owns one direction and the labels say so.
class RlcdFrontPanel {
 public:
  enum class Mode : uint8_t { kSplash, kLive, kMenu, kEdit, kPower, kTempo };
  enum class Action : uint8_t { kNone, kReboot, kPowerOff };

  static constexpr int kItems = 9;  // 8 settings + POWER
  static constexpr int kPowerItem = 8;
  static constexpr int64_t kIdleUs = 25000000;
  static constexpr int64_t kTempoIdleUs = 3000000;  // Tempo screen auto-close
  static constexpr int64_t kSplashUs = 6000000;
  static constexpr int kPowerChoices = 3;  // Restart, Power Off, Cancel

  explicit RlcdFrontPanel(Config* cfg);

  void on_key_short(int64_t now_us);
  void on_key_long(int64_t now_us);
  void on_key_repeat(int64_t now_us);
  void on_boot_short(int64_t now_us);
  void on_boot_long(int64_t now_us);
  void on_boot_repeat(int64_t now_us);
  void tick(int64_t now_us);

  // Force the live face (drops any open menu/tempo/power overlay). The
  // rlcd task calls this after an orientation flip so the rotated screen
  // comes up on the status face rather than a half-navigated menu.
  void show_live(int64_t now_us) {
    touch(now_us);
    mode_ = Mode::kLive;
    cursor_ = 0;
  }

  Mode mode() const { return mode_; }
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
  void leave_splash(int64_t now_us);
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
