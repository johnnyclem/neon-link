#include "neon/ui/menu_model.hpp"

#include <cstdio>

#include "neon/ui/theme_gen.hpp"

namespace neon {

namespace {

int clamp_int(int v, int lo, int hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

int wrap_int(int v, int n) { return ((v % n) + n) % n; }

const char* reset_mode_name(ResetMode m) {
  switch (m) {
    case ResetMode::kStartOfPlay:
      return "START";
    case ResetMode::kEveryBar:
      return "BAR";
    case ResetMode::kAtStop:
      return "STOP";
    default:
      return "OFF";
  }
}

const char* role_name(OutputRole r) {
  switch (r) {
    case OutputRole::kGate:
      return "GATE";
    case OutputRole::kResetLoop:
      return "RST LP";
    case OutputRole::kResetStart:
      return "RST ST";
    case OutputRole::kResetStop:
      return "RST SP";
    default:
      return "CLOCK";
  }
}

const char* rhythm_name(ClockOutputConfig::RhythmMode m) {
  switch (m) {
    case ClockOutputConfig::RhythmMode::kEuclid:
      return "EUCLID";
    case ClockOutputConfig::RhythmMode::kProbability:
      return "CHANCE";
    case ClockOutputConfig::RhythmMode::kPattern:
      return "STEPS";
    default:
      return "ALL";
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

void gate_target_name(uint8_t target, char* buf, int cap) {
  if (target == MidiRouteConfig::kTargetNone) {
    std::snprintf(buf, cap, "OFF");
  } else if (target == MidiRouteConfig::kTargetRun) {
    std::snprintf(buf, cap, "RUN");
  } else {
    std::snprintf(buf, cap, "CLK%u", static_cast<unsigned>(target + 1));
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
    case Screen::kMidi:
      return kMidiItems;
    case Screen::kSystem:
      return kSystemItems;
    default:
      // Home, Network and Confirm carry no selectable list.
      return 0;
  }
}

const char* MenuModel::screen_title() const {
  switch (screen_) {
    case Screen::kHome:
      return ui::kTitleLive;
    case Screen::kMenu:
      return "MENU";
    case Screen::kOutputs:
      return ui::kTitleOutputs;
    case Screen::kOutputEdit:
      return "CLK";
    case Screen::kNetwork:
      return ui::kTitleNetwork;
    case Screen::kMidi:
      return ui::kTitleMidi;
    case Screen::kSystem:
      return ui::kTitleSystem;
    case Screen::kConfirm:
      return "CONFIRM";
  }
  return "";
}

void MenuModel::on_rotate(int detents) {
  if (detents == 0) {
    return;
  }
  if (screen_ == Screen::kConfirm) {
    confirm_yes_ = !confirm_yes_;
    return;
  }
  if (editing_) {
    if (screen_ == Screen::kOutputEdit) {
      adjust_output_param(cursor_, detents);
    } else if (screen_ == Screen::kMidi) {
      adjust_midi(cursor_, detents);
    } else if (screen_ == Screen::kSystem) {
      adjust_system(cursor_, detents);
    }
    return;
  }
  const int n = item_count();
  if (n == 0) {
    return;
  }
  cursor_ = wrap_int(cursor_ + detents, n);
}

void MenuModel::on_click() {
  switch (screen_) {
    case Screen::kHome:
      screen_ = Screen::kMenu;
      cursor_ = 0;
      break;

    case Screen::kMenu:
      switch (cursor_) {
        case 0:
          screen_ = Screen::kHome;
          break;
        case 1:
          screen_ = Screen::kOutputs;
          break;
        case 2:
          screen_ = Screen::kNetwork;
          break;
        case 3:
          screen_ = Screen::kMidi;
          break;
        default:
          screen_ = Screen::kSystem;
          break;
      }
      cursor_ = 0;
      break;

    case Screen::kOutputs:
      output_ = cursor_;
      screen_ = Screen::kOutputEdit;
      cursor_ = 0;
      break;

    case Screen::kOutputEdit:
    case Screen::kMidi:
      editing_ = !editing_;
      break;

    case Screen::kSystem:
      if (cursor_ == kSystemRebootItem) {
        screen_ = Screen::kConfirm;
        confirm_yes_ = false;
      } else {
        editing_ = !editing_;
      }
      break;

    case Screen::kNetwork:
      // Read-only: network credentials belong to the web UI, where there
      // is a keyboard.
      screen_ = Screen::kMenu;
      cursor_ = 2;
      break;

    case Screen::kConfirm:
      if (confirm_yes_) {
        action_ = Action::kReboot;
        screen_ = Screen::kHome;
      } else {
        screen_ = Screen::kSystem;
        cursor_ = kSystemRebootItem;
      }
      confirm_yes_ = false;
      break;
  }
}

void MenuModel::on_long_press() {
  // An in-progress edit swallows the gesture: the first long press is the
  // escape from edit mode, the next one leaves the screen.
  if (editing_) {
    editing_ = false;
    return;
  }

  switch (screen_) {
    case Screen::kHome:
      break;
    case Screen::kMenu:
      screen_ = Screen::kHome;
      cursor_ = 0;
      break;
    case Screen::kOutputEdit:
      screen_ = Screen::kOutputs;
      cursor_ = output_;
      break;
    case Screen::kConfirm:
      screen_ = Screen::kSystem;
      cursor_ = kSystemRebootItem;
      confirm_yes_ = false;
      break;
    case Screen::kOutputs:
      screen_ = Screen::kMenu;
      cursor_ = 1;
      break;
    case Screen::kNetwork:
      screen_ = Screen::kMenu;
      cursor_ = 2;
      break;
    case Screen::kMidi:
      screen_ = Screen::kMenu;
      cursor_ = 3;
      break;
    case Screen::kSystem:
      screen_ = Screen::kMenu;
      cursor_ = 4;
      break;
  }
}

bool MenuModel::take_dirty() {
  const bool d = dirty_;
  dirty_ = false;
  return d;
}

MenuModel::Action MenuModel::take_action() {
  const Action a = action_;
  action_ = Action::kNone;
  return a;
}

const char* MenuModel::item_label(int index) const {
  switch (screen_) {
    case Screen::kMenu: {
      static const char* kItems[kMenuItems] = {
          ui::kTitleLive, ui::kTitleOutputs, ui::kTitleNetwork, ui::kTitleMidi,
          ui::kTitleSystem};
      return kItems[clamp_int(index, 0, kMenuItems - 1)];
    }
    case Screen::kOutputs: {
      static const char* kItems[kOutputsItems] = {"CLK 1", "CLK 2", "CLK 3",
                                                  "CLK 4"};
      return kItems[clamp_int(index, 0, kOutputsItems - 1)];
    }
    case Screen::kOutputEdit: {
      static const char* kItems[kOutputEditItems] = {
          "ENABLED",  "PPQN",     "MULT",     "DIV",      "MODE",
          "TRIG MS",  "DUTY",     "SHUF",     "ROLE",     "FREE RUN",
          "RHYTHM",   "STEPS",    "FILLS",    "ROT",      "CHANCE",
          "JITTER",   "PER LOOP"};
      return kItems[clamp_int(index, 0, kOutputEditItems - 1)];
    }
    case Screen::kMidi: {
      static const char* kItems[kMidiItems] = {"BLE", "CLK OUT", "CHANNEL",
                                               "GATE", "PITCH CV"};
      return kItems[clamp_int(index, 0, kMidiItems - 1)];
    }
    case Screen::kSystem: {
      static const char* kItems[kSystemItems] = {
          "LATENCY",  "RESET",    "SOURCE",   "IN PPQN",
          "GATE CLK", "QUANTUM",  "RST EDGE", "MIDI NDG",
          "SS SYNC",  "BRIGHT",   "BEAT",     "REBOOT"};
      return kItems[clamp_int(index, 0, kSystemItems - 1)];
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
      case 8:
        std::snprintf(buf, cap, "%s", role_name(c.role));
        break;
      case 9:
        std::snprintf(buf, cap, "%s", c.free_run ? "ON" : "OFF");
        break;
      case 10:
        std::snprintf(buf, cap, "%s", rhythm_name(c.rhythm));
        break;
      case 11:
        std::snprintf(buf, cap, "%u", static_cast<unsigned>(c.euclid_steps));
        break;
      case 12:
        std::snprintf(buf, cap, "%u", static_cast<unsigned>(c.euclid_fills));
        break;
      case 13:
        std::snprintf(buf, cap, "%u", static_cast<unsigned>(c.euclid_rot));
        break;
      case 14:
        std::snprintf(buf, cap, "%u%%",
                      static_cast<unsigned>(c.probability_pct));
        break;
      case 15:
        std::snprintf(buf, cap, "%u%%", static_cast<unsigned>(c.humanize_pct));
        break;
      case 16:
        std::snprintf(buf, cap, "%s", c.rhythm_over_loop ? "ON" : "OFF");
        break;
      default:
        break;
    }
  } else if (screen_ == Screen::kMidi) {
    switch (index) {
      case 0:
        std::snprintf(buf, cap, "%s", cfg_->ble_enabled ? "ON" : "OFF");
        break;
      case 1:
        std::snprintf(buf, cap, "%s", cfg_->midi_clock_out ? "ON" : "OFF");
        break;
      case 2:
        if (cfg_->midi.midi_channel > 15) {
          std::snprintf(buf, cap, "OMNI");
        } else {
          std::snprintf(buf, cap, "CH%u",
                        static_cast<unsigned>(cfg_->midi.midi_channel + 1));
        }
        break;
      case 3:
        gate_target_name(cfg_->midi.gate_target, buf, cap);
        break;
      case 4:
        std::snprintf(buf, cap, "%s", cfg_->midi.pitch_cv ? "ON" : "OFF");
        break;
      default:
        break;
    }
  } else if (screen_ == Screen::kSystem) {
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
      case 5:
        std::snprintf(buf, cap, "%u", static_cast<unsigned>(cfg_->quantum_beats));
        break;
      case 6:
        std::snprintf(buf, cap, "%s",
                      cfg_->engine.reset_before_edge ? "LEAD" : "ON");
        break;
      case 7:
        std::snprintf(buf, cap, "%+.1f", cfg_->midi_nudge_us / 1000.0);
        break;
      case 8:
        std::snprintf(buf, cap, "%s", cfg_->start_stop_sync ? "ON" : "OFF");
        break;
      case 9:
        std::snprintf(buf, cap, "%u",
                      static_cast<unsigned>(cfg_->display_brightness));
        break;
      case 10:
        std::snprintf(buf, cap, "%s", cfg_->big_beat_display ? "ON" : "OFF");
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
    case 8:
      c.role = static_cast<OutputRole>(
          wrap_int(static_cast<int>(c.role) + delta, 5));
      break;
    case 9:
      c.free_run = delta > 0;
      break;
    case 10:
      c.rhythm = static_cast<ClockOutputConfig::RhythmMode>(
          wrap_int(static_cast<int>(c.rhythm) + delta, 4));
      break;
    case 11:
      c.euclid_steps = static_cast<uint8_t>(
          clamp_int(static_cast<int>(c.euclid_steps) + delta, 1, 64));
      break;
    case 12:
      c.euclid_fills = static_cast<uint8_t>(
          clamp_int(static_cast<int>(c.euclid_fills) + delta, 0, 64));
      break;
    case 13:
      c.euclid_rot = static_cast<uint8_t>(
          clamp_int(static_cast<int>(c.euclid_rot) + delta, 0, 63));
      break;
    case 14:
      c.probability_pct = static_cast<uint8_t>(
          clamp_int(static_cast<int>(c.probability_pct) + delta, 0, 100));
      break;
    case 15:
      c.humanize_pct = static_cast<uint8_t>(
          clamp_int(static_cast<int>(c.humanize_pct) + delta, 0, 50));
      break;
    case 16:
      c.rhythm_over_loop = delta > 0;
      break;
    default:
      return;
  }
  mark_dirty();
}

void MenuModel::adjust_midi(int index, int delta) {
  switch (index) {
    case 0:
      cfg_->ble_enabled = delta > 0 ? 1 : 0;
      break;
    case 1:
      cfg_->midi_clock_out = delta > 0 ? 1 : 0;
      break;
    case 2: {
      // 0..15 are channels, 16 wraps back to omni.
      const int current =
          cfg_->midi.midi_channel > 15 ? 16 : cfg_->midi.midi_channel;
      const int next = wrap_int(current + delta, 17);
      cfg_->midi.midi_channel =
          next == 16 ? MidiRouteConfig::kTargetNone
                     : static_cast<uint8_t>(next);
      break;
    }
    case 3: {
      // 0..3 CLK1..4, 4 RUN, 5 wraps to off.
      const int current =
          cfg_->midi.gate_target == MidiRouteConfig::kTargetNone
              ? 5
              : cfg_->midi.gate_target;
      const int next = wrap_int(current + delta, 6);
      cfg_->midi.gate_target = next == 5
                                   ? MidiRouteConfig::kTargetNone
                                   : static_cast<uint8_t>(next);
      break;
    }
    case 4:
      cfg_->midi.pitch_cv = delta > 0;
      break;
    default:
      return;
  }
  mark_dirty();
}

void MenuModel::adjust_system(int index, int delta) {
  switch (index) {
    case 0:
      cfg_->engine.latency_us = clamp_int(
          cfg_->engine.latency_us + delta * 100, -50000, 50000);
      break;
    case 1: {
      // START / BAR / OFF / STOP.
      const int m = static_cast<int>(cfg_->engine.reset_mode) + delta;
      cfg_->engine.reset_mode = static_cast<ResetMode>(wrap_int(m, 4));
      break;
    }
    case 2: {
      const int s = static_cast<int>(cfg_->clock_source) + delta;
      cfg_->clock_source = static_cast<ClockSource>(wrap_int(s, 3));
      break;
    }
    case 3:
      cfg_->clock_in_ppqn = static_cast<uint32_t>(
          clamp_int(static_cast<int>(cfg_->clock_in_ppqn) + delta, 1, 96));
      break;
    case 4:
      cfg_->engine.transport_gating = delta > 0;
      break;
    case 5:
      cfg_->quantum_beats = static_cast<uint32_t>(
          clamp_int(static_cast<int>(cfg_->quantum_beats) + delta, 1, 16));
      // The engine derives its loop-rate channels from its own copy.
      cfg_->engine.quantum_beats = cfg_->quantum_beats;
      break;
    case 6:
      cfg_->engine.reset_before_edge = delta > 0;
      break;
    case 7:
      cfg_->midi_nudge_us =
          clamp_int(cfg_->midi_nudge_us + delta * 500, -100000, 100000);
      break;
    case 8:
      cfg_->start_stop_sync = delta > 0 ? 1 : 0;
      break;
    case 9:
      cfg_->display_brightness = static_cast<uint8_t>(clamp_int(
          static_cast<int>(cfg_->display_brightness) + delta * 8, 0, 255));
      break;
    case 10:
      cfg_->big_beat_display = delta > 0 ? 1 : 0;
      break;
    default:
      return;
  }
  mark_dirty();
}

}  // namespace neon
