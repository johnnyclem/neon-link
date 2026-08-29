#include "neon/ui/rlcd_front.hpp"

#include <cstdio>

namespace neon {
namespace {

int wrap(int v, int n) { return ((v % n) + n) % n; }

const uint32_t kQuantum[] = {1, 2, 4, 8};
constexpr int kQuantumN = 4;

int quantum_index(uint32_t q) {
  for (int i = 0; i < kQuantumN; ++i) {
    if (kQuantum[i] == q) {
      return i;
    }
  }
  return 2;
}

}  // namespace

RlcdFrontPanel::RlcdFrontPanel(Config* cfg) : cfg_(cfg) {}

bool RlcdFrontPanel::readonly(int index) const { return index == 0; }

void RlcdFrontPanel::touch(int64_t now_us) { last_us_ = now_us; }

void RlcdFrontPanel::leave_splash(int64_t now_us) {
  touch(now_us);
  mode_ = Mode::kLive;
}

void RlcdFrontPanel::on_key_short(int64_t now_us) {
  touch(now_us);
  switch (mode_) {
    case Mode::kSplash:
      leave_splash(now_us);
      return;
    case Mode::kLive:
      toggle_ = true;
      return;
    case Mode::kTempo:
      // In the Tempo screen KEY is the down button.
      nudge_ -= 1;
      return;
    case Mode::kMenu:
      if (cursor_ == kPowerItem) {
        mode_ = Mode::kPower;
        power_cur_ = 2;
        return;
      }
      if (readonly(cursor_)) {
        return;
      }
      stash();
      mode_ = Mode::kEdit;
      return;
    case Mode::kEdit:
      dirty_ = true;
      mode_ = Mode::kMenu;
      return;
    case Mode::kPower:
      if (power_cur_ == 0) {
        action_ = Action::kReboot;
      } else if (power_cur_ == 1) {
        action_ = Action::kPowerOff;
      }
      mode_ = (power_cur_ == 2) ? Mode::kLive : Mode::kSplash;
      return;
  }
}

void RlcdFrontPanel::on_key_long(int64_t now_us) {
  touch(now_us);
  switch (mode_) {
    case Mode::kSplash:
      leave_splash(now_us);
      return;
    case Mode::kLive:
      mode_ = Mode::kMenu;
      cursor_ = 0;
      return;
    case Mode::kTempo:
      // KEY held ramps down; the repeats that follow keep falling.
      nudge_ -= 1;
      return;
    case Mode::kMenu:
      mode_ = Mode::kLive;
      return;
    case Mode::kEdit:
      revert();
      mode_ = Mode::kMenu;
      return;
    case Mode::kPower:
      mode_ = Mode::kLive;
      return;
  }
}

// Auto-repeat only means something on the Tempo screen, where holding a
// button walks the tempo. Every other mode ignores it (a held button
// there has already done its one job at the long-press threshold).
void RlcdFrontPanel::on_key_repeat(int64_t now_us) {
  if (mode_ != Mode::kTempo) {
    return;
  }
  touch(now_us);
  nudge_ -= 1;
}

void RlcdFrontPanel::on_boot_repeat(int64_t now_us) {
  if (mode_ != Mode::kTempo) {
    return;
  }
  touch(now_us);
  nudge_ += 1;
}

void RlcdFrontPanel::on_boot_short(int64_t now_us) {
  touch(now_us);
  switch (mode_) {
    case Mode::kSplash:
      leave_splash(now_us);
      return;
    case Mode::kLive:
      nudge_ += 1;
      return;
    case Mode::kTempo:
      // In the Tempo screen BOOT is the up button.
      nudge_ += 1;
      return;
    case Mode::kMenu:
      cursor_ = wrap(cursor_ + 1, kItems);
      return;
    case Mode::kEdit:
      step(1);
      return;
    case Mode::kPower:
      power_cur_ = wrap(power_cur_ + 1, kPowerChoices);
      return;
  }
}

void RlcdFrontPanel::on_boot_long(int64_t now_us) {
  touch(now_us);
  switch (mode_) {
    case Mode::kSplash:
      leave_splash(now_us);
      return;
    case Mode::kLive:
      // Open the Tempo screen. Keep holding and the repeats that follow
      // ramp the tempo up without a second press.
      mode_ = Mode::kTempo;
      return;
    case Mode::kTempo:
      nudge_ += 1;
      return;
    case Mode::kMenu:
      cursor_ = wrap(cursor_ - 1, kItems);
      return;
    case Mode::kEdit:
      step(-1);
      return;
    case Mode::kPower:
      power_cur_ = wrap(power_cur_ - 1, kPowerChoices);
      return;
  }
}

void RlcdFrontPanel::tick(int64_t now_us) {
  if (mode_ == Mode::kSplash) {
    if (last_us_ == 0) {
      last_us_ = now_us;
    } else if (now_us - last_us_ >= kSplashUs) {
      leave_splash(now_us);
    }
    return;
  }
  if (mode_ == Mode::kLive) {
    return;
  }
  const int64_t idle = (mode_ == Mode::kTempo) ? kTempoIdleUs : kIdleUs;
  if (last_us_ != 0 && now_us - last_us_ >= idle) {
    if (mode_ == Mode::kEdit) {
      revert();
    }
    mode_ = Mode::kLive;
  }
}

bool RlcdFrontPanel::take_dirty() {
  const bool d = dirty_;
  dirty_ = false;
  return d;
}

RlcdFrontPanel::Action RlcdFrontPanel::take_action() {
  const Action a = action_;
  action_ = Action::kNone;
  return a;
}

int RlcdFrontPanel::take_nudge() {
  const int n = nudge_;
  nudge_ = 0;
  return n;
}

bool RlcdFrontPanel::take_toggle() {
  const bool t = toggle_;
  toggle_ = false;
  return t;
}

const char* RlcdFrontPanel::item_label(int index) const {
  static const char* kLabels[kItems] = {"PPQN",    "TRS",      "AP",
                                        "QUANTUM", "SS SYNC",  "MIDI CLK",
                                        "POWER"};
  if (index < 0 || index >= kItems) {
    return "";
  }
  return kLabels[index];
}

void RlcdFrontPanel::item_value(int index, char* buf, int cap) const {
  if (buf == nullptr || cap < 1 || cfg_ == nullptr) {
    return;
  }
  buf[0] = '\0';
  switch (index) {
    case 0:
      std::snprintf(buf, cap, "24");
      break;
    case 1:
      std::snprintf(buf, cap, "%s", cfg_->midi_trs_type ? "B" : "A");
      break;
    case 2:
      std::snprintf(buf, cap, "%s",
                    cfg_->ap_policy == ApPolicy::kAlways    ? "ALWAYS"
                    : cfg_->ap_policy == ApPolicy::kOff     ? "OFF"
                                                            : "FALLBACK");
      break;
    case 3:
      std::snprintf(buf, cap, "%u",
                    static_cast<unsigned>(cfg_->quantum_beats));
      break;
    case 4:
      std::snprintf(buf, cap, "%s", cfg_->start_stop_sync ? "ON" : "OFF");
      break;
    case 5:
      std::snprintf(buf, cap, "%s", cfg_->midi_clock_out ? "ON" : "OFF");
      break;
    case 6:
      std::snprintf(buf, cap, ">");
      break;
    default:
      break;
  }
}

const char* RlcdFrontPanel::power_label(int index) {
  static const char* kLabels[kPowerChoices] = {"RESTART", "POWER OFF",
                                               "CANCEL"};
  if (index < 0 || index >= kPowerChoices) {
    return "";
  }
  return kLabels[index];
}

void RlcdFrontPanel::stash() {
  if (cfg_ == nullptr) {
    return;
  }
  switch (cursor_) {
    case 1:
      stash8_ = cfg_->midi_trs_type;
      break;
    case 2:
      stash8_ = static_cast<uint8_t>(cfg_->ap_policy);
      break;
    case 3:
      stash32_ = cfg_->quantum_beats;
      break;
    case 4:
      stash8_ = cfg_->start_stop_sync;
      break;
    case 5:
      stash8_ = cfg_->midi_clock_out;
      break;
    default:
      break;
  }
}

void RlcdFrontPanel::revert() {
  if (cfg_ == nullptr) {
    return;
  }
  switch (cursor_) {
    case 1:
      cfg_->midi_trs_type = stash8_;
      break;
    case 2:
      cfg_->ap_policy = static_cast<ApPolicy>(stash8_);
      break;
    case 3:
      cfg_->quantum_beats = stash32_;
      break;
    case 4:
      cfg_->start_stop_sync = stash8_;
      break;
    case 5:
      cfg_->midi_clock_out = stash8_;
      break;
    default:
      break;
  }
}

void RlcdFrontPanel::step(int delta) {
  if (cfg_ == nullptr) {
    return;
  }
  switch (cursor_) {
    case 1:
      cfg_->midi_trs_type = cfg_->midi_trs_type ? 0 : 1;
      break;
    case 2: {
      int p = static_cast<int>(cfg_->ap_policy) + (delta > 0 ? 1 : -1);
      if (p < 0) {
        p = 2;
      }
      if (p > 2) {
        p = 0;
      }
      cfg_->ap_policy = static_cast<ApPolicy>(p);
      break;
    }
    case 3: {
      int i = wrap(quantum_index(cfg_->quantum_beats) + (delta > 0 ? 1 : -1),
                   kQuantumN);
      cfg_->quantum_beats = kQuantum[i];
      break;
    }
    case 4:
      cfg_->start_stop_sync = cfg_->start_stop_sync ? 0 : 1;
      break;
    case 5:
      cfg_->midi_clock_out = cfg_->midi_clock_out ? 0 : 1;
      break;
    default:
      break;
  }
}

}  // namespace neon
