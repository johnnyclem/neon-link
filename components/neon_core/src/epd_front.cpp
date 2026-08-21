#include "neon/ui/epd_front.hpp"

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

EpdFrontPanel::EpdFrontPanel(Config* cfg) : cfg_(cfg) {}

bool EpdFrontPanel::readonly(int index) const { return index == 0; }

void EpdFrontPanel::touch(int64_t now_us) { last_us_ = now_us; }

void EpdFrontPanel::on_up(int64_t now_us) {
  touch(now_us);
  if (mode_ == Mode::kSplash) {
    return;
  }
  if (mode_ == Mode::kLive) {
    nudge_ += 1;
    return;
  }
  if (mode_ == Mode::kPower) {
    power_cur_ = wrap(power_cur_ - 1, kPowerChoices);
    return;
  }
  if (mode_ == Mode::kEdit) {
    step(1);
    return;
  }
  cursor_ = wrap(cursor_ - 1, kItems);
}

void EpdFrontPanel::on_down(int64_t now_us) {
  touch(now_us);
  if (mode_ == Mode::kSplash) {
    return;
  }
  if (mode_ == Mode::kLive) {
    nudge_ -= 1;
    return;
  }
  if (mode_ == Mode::kPower) {
    power_cur_ = wrap(power_cur_ + 1, kPowerChoices);
    return;
  }
  if (mode_ == Mode::kEdit) {
    step(-1);
    return;
  }
  cursor_ = wrap(cursor_ + 1, kItems);
}

void EpdFrontPanel::on_confirm(int64_t now_us) {
  touch(now_us);
  if (mode_ == Mode::kSplash) {
    return;
  }
  if (mode_ == Mode::kLive) {
    toggle_ = true;
    return;
  }
  if (mode_ == Mode::kPower) {
    if (power_cur_ == 0) {
      action_ = Action::kReboot;
    } else if (power_cur_ == 1) {
      action_ = Action::kPowerOff;
    }
    mode_ = (power_cur_ == 2) ? Mode::kLive : Mode::kSplash;
    return;
  }
  if (mode_ == Mode::kMenu) {
    if (readonly(cursor_)) {
      return;
    }
    stash();
    mode_ = Mode::kEdit;
    return;
  }
  dirty_ = true;
  mode_ = Mode::kMenu;
}

void EpdFrontPanel::on_cancel(int64_t now_us) {
  touch(now_us);
  if (mode_ == Mode::kSplash) {
    return;
  }
  if (mode_ == Mode::kEdit) {
    revert();
    mode_ = Mode::kMenu;
    return;
  }
  if (mode_ == Mode::kPower) {
    mode_ = Mode::kLive;
  }
}

void EpdFrontPanel::on_chord(int64_t now_us) {
  touch(now_us);
  if (mode_ == Mode::kSplash) {
    mode_ = Mode::kLive;
    return;
  }
  if (mode_ == Mode::kLive) {
    mode_ = Mode::kMenu;
    cursor_ = 0;
    return;
  }
  if (mode_ == Mode::kEdit) {
    revert();
  }
  mode_ = Mode::kLive;
}

void EpdFrontPanel::on_cancel_long(int64_t now_us) {
  touch(now_us);
  if (mode_ == Mode::kLive) {
    mode_ = Mode::kPower;
    power_cur_ = 2;
  }
}

void EpdFrontPanel::tick(int64_t now_us) {
  if (mode_ == Mode::kLive || mode_ == Mode::kSplash) {
    return;
  }
  if (last_us_ != 0 && now_us - last_us_ >= kIdleUs) {
    if (mode_ == Mode::kEdit) {
      revert();
    }
    mode_ = Mode::kLive;
  }
}

bool EpdFrontPanel::take_dirty() {
  const bool d = dirty_;
  dirty_ = false;
  return d;
}

EpdFrontPanel::Action EpdFrontPanel::take_action() {
  const Action a = action_;
  action_ = Action::kNone;
  return a;
}

int EpdFrontPanel::take_nudge() {
  const int n = nudge_;
  nudge_ = 0;
  return n;
}

bool EpdFrontPanel::take_toggle() {
  const bool t = toggle_;
  toggle_ = false;
  return t;
}

const char* EpdFrontPanel::item_label(int index) const {
  static const char* kLabels[kItems] = {"PPQN",   "TRS",     "AP",
                                        "QUANTUM", "SS SYNC", "MIDI CLK"};
  if (index < 0 || index >= kItems) {
    return "";
  }
  return kLabels[index];
}

void EpdFrontPanel::item_value(int index, char* buf, int cap) const {
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
    default:
      break;
  }
}

const char* EpdFrontPanel::power_label(int index) {
  static const char* kLabels[kPowerChoices] = {"RESTART", "POWER OFF",
                                               "CANCEL"};
  if (index < 0 || index >= kPowerChoices) {
    return "";
  }
  return kLabels[index];
}

void EpdFrontPanel::stash() {
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

void EpdFrontPanel::revert() {
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

void EpdFrontPanel::step(int delta) {
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
