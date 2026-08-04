#include "neon/ui/menu_model.hpp"

#include <cstdio>

namespace neon {

namespace {

int clamp_int(int v, int lo, int hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

const char* reset_mode_name(ResetMode m) {
  switch (m) {
    case ResetMode::kStartOfPlay:
      return "START";
    case ResetMode::kEveryBar:
      return "BAR";
    default:
      return "OFF";
  }
}

const char* clock_source_name(ClockSource s) {
  switch (s) {
    case ClockSource::kLinkMaster:
      return "LINK";
    case ClockSource::kExternalMaster:
      return "EXT";
    default:
      return "AUTO";
  }
}

}  // namespace

int MenuModel::item_count() const {
  switch (screen_) {
    case Screen::kMenu:
      return kMenuItems;
    case Screen::kOutputs:
      return kOutputsItems;
    case Screen::kOutputEdit:
      return kOutputEditItems;
    case Screen::kSettings:
      return kSettingsItems;
    default:
      return 0;
  }
}

void MenuModel::on_rotate(int detents) {
  if (detents == 0) {
    return;
  }
  if (screen_ == Screen::kHome) {
    return;
  }
  if (editing_) {
    if (screen_ == Screen::kOutputEdit) {
      adjust_output_param(cursor_, detents);
    } else if (screen_ == Screen::kSettings) {
      adjust_setting(cursor_, detents);
    }
    return;
  }
  const int n = item_count();
  cursor_ = ((cursor_ + detents) % n + n) % n;
}

void MenuModel::on_click() {
  switch (screen_) {
    case Screen::kHome:
      screen_ = Screen::kMenu;
      cursor_ = 0;
      break;
    case Screen::kMenu:
      if (cursor_ == 0) {
        screen_ = Screen::kOutputs;
      } else if (cursor_ == 1) {
        screen_ = Screen::kSettings;
      } else {
        screen_ = Screen::kHome;
      }
      cursor_ = 0;
      break;
    case Screen::kOutputs:
      if (cursor_ < 4) {
        output_ = cursor_;
        screen_ = Screen::kOutputEdit;
        cursor_ = 0;
      } else {
        screen_ = Screen::kMenu;
        cursor_ = 0;
      }
      break;
    case Screen::kOutputEdit:
      if (cursor_ == kOutputEditItems - 1) {  // Back
        screen_ = Screen::kOutputs;
        cursor_ = output_;
        editing_ = false;
      } else {
        editing_ = !editing_;
      }
      break;
    case Screen::kSettings:
      if (cursor_ == kSettingsItems - 1) {  // Back
        screen_ = Screen::kMenu;
        cursor_ = 1;
        editing_ = false;
      } else {
        editing_ = !editing_;
      }
      break;
  }
}

bool MenuModel::take_dirty() {
  const bool d = dirty_;
  dirty_ = false;
  return d;
}

const char* MenuModel::item_label(int index) const {
  switch (screen_) {
    case Screen::kMenu: {
      static const char* kItems[kMenuItems] = {"OUTPUTS", "SETTINGS", "BACK"};
      return kItems[clamp_int(index, 0, kMenuItems - 1)];
    }
    case Screen::kOutputs: {
      static const char* kItems[kOutputsItems] = {"CLK 1", "CLK 2", "CLK 3",
                                                  "CLK 4", "BACK"};
      return kItems[clamp_int(index, 0, kOutputsItems - 1)];
    }
    case Screen::kOutputEdit: {
      static const char* kItems[kOutputEditItems] = {
          "ENABLED", "PPQN", "MULT", "DIV",    "MODE",
          "TRIG MS", "DUTY", "SHUF", "BACK"};
      return kItems[clamp_int(index, 0, kOutputEditItems - 1)];
    }
    case Screen::kSettings: {
      static const char* kItems[kSettingsItems] = {
          "LATENCY", "RESET", "SOURCE", "IN PPQN", "GATE CLK", "BACK"};
      return kItems[clamp_int(index, 0, kSettingsItems - 1)];
    }
    default:
      return "";
  }
}

void MenuModel::item_value(int index, char* buf, int cap) const {
  buf[0] = '\0';
  if (screen_ == Screen::kOutputEdit) {
    const ClockOutputConfig& c = cfg_->engine.clocks[output_];
    switch (index) {
      case 0:
        std::snprintf(buf, cap, "%s", c.enabled ? "ON" : "OFF");
        break;
      case 1:
        std::snprintf(buf, cap, "%u", static_cast<unsigned>(c.ppqn));
        break;
      case 2:
        std::snprintf(buf, cap, "x%u", static_cast<unsigned>(c.mult));
        break;
      case 3:
        std::snprintf(buf, cap, "/%u", static_cast<unsigned>(c.div));
        break;
      case 4:
        std::snprintf(buf, cap, "%s",
                      c.mode == ClockOutputConfig::PulseMode::kSquare
                          ? "SQR"
                          : "TRIG");
        break;
      case 5:
        std::snprintf(buf, cap, "%u", static_cast<unsigned>(c.trig_len_us / 1000));
        break;
      case 6:
        std::snprintf(buf, cap, "%u%%", static_cast<unsigned>(c.duty_pct));
        break;
      case 7:
        std::snprintf(buf, cap, "%u%%", static_cast<unsigned>(c.shuffle_pct));
        break;
      default:
        break;
    }
  } else if (screen_ == Screen::kSettings) {
    switch (index) {
      case 0:
        std::snprintf(buf, cap, "%+.1f", cfg_->engine.latency_us / 1000.0);
        break;
      case 1:
        std::snprintf(buf, cap, "%s", reset_mode_name(cfg_->engine.reset_mode));
        break;
      case 2:
        std::snprintf(buf, cap, "%s", clock_source_name(cfg_->clock_source));
        break;
      case 3:
        std::snprintf(buf, cap, "%u", static_cast<unsigned>(cfg_->clock_in_ppqn));
        break;
      case 4:
        std::snprintf(buf, cap, "%s",
                      cfg_->engine.transport_gating ? "ON" : "OFF");
        break;
      default:
        break;
    }
  }
}

void MenuModel::adjust_output_param(int index, int delta) {
  ClockOutputConfig& c = cfg_->engine.clocks[output_];
  switch (index) {
    case 0:
      c.enabled = delta > 0;
      break;
    case 1:
      c.ppqn = static_cast<uint32_t>(
          clamp_int(static_cast<int>(c.ppqn) + delta, 1, 192));
      break;
    case 2:
      c.mult = static_cast<uint32_t>(
          clamp_int(static_cast<int>(c.mult) + delta, 1, 16));
      break;
    case 3:
      c.div = static_cast<uint32_t>(
          clamp_int(static_cast<int>(c.div) + delta, 1, 16));
      break;
    case 4:
      c.mode = delta > 0 ? ClockOutputConfig::PulseMode::kSquare
                         : ClockOutputConfig::PulseMode::kTrigger;
      break;
    case 5:
      c.trig_len_us = static_cast<uint32_t>(
          clamp_int(static_cast<int>(c.trig_len_us) + delta * 1000, 1000,
                    100000));
      break;
    case 6:
      c.duty_pct = static_cast<uint8_t>(
          clamp_int(static_cast<int>(c.duty_pct) + delta, 1, 99));
      break;
    case 7:
      c.shuffle_pct = static_cast<uint8_t>(
          clamp_int(static_cast<int>(c.shuffle_pct) + delta, 0, 75));
      break;
    default:
      return;
  }
  mark_dirty();
}

void MenuModel::adjust_setting(int index, int delta) {
  switch (index) {
    case 0:
      cfg_->engine.latency_us = clamp_int(
          cfg_->engine.latency_us + delta * 100, -50000, 50000);
      break;
    case 1: {
      int m = static_cast<int>(cfg_->engine.reset_mode) + delta;
      cfg_->engine.reset_mode = static_cast<ResetMode>(((m % 3) + 3) % 3);
      break;
    }
    case 2: {
      int s = static_cast<int>(cfg_->clock_source) + delta;
      cfg_->clock_source = static_cast<ClockSource>(((s % 3) + 3) % 3);
      break;
    }
    case 3:
      cfg_->clock_in_ppqn = static_cast<uint32_t>(
          clamp_int(static_cast<int>(cfg_->clock_in_ppqn) + delta, 1, 96));
      break;
    case 4:
      cfg_->engine.transport_gating = delta > 0;
      break;
    default:
      return;
  }
  mark_dirty();
}

}  // namespace neon
