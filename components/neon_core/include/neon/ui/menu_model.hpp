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
  // Which beat animation the live screen draws. Default number.
  uint8_t beat_style = 0;
  // Running firmware version (esp_app_desc_t.version — a git describe
  // string, not hand-maintained), for the System screen's VERSION row
  // (G5 in the ship-gate review: you cannot support a unit in another
  // city without knowing what it is running). Empty on host builds.
  char firmware[32] = {};
  // This unit's setup AP password (Config::ap_pass) and the OTA/
  // factory-reset device token (Config::device_token), both per-device
  // secrets generated at first boot with no other display surface — the
  // web UI never round-trips the AP password, and the token exists
  // specifically because a non-browser client can forge the check the web
  // UI passes. The Network screen shows the password only while the
  // setup AP is actually up (render_ui, not this struct, enforces that).
  char ap_pass[65] = {};
  char device_token[33] = {};
  // Free-running counter at neon::ui::kIconTickHz, driving the icon loops
  // that have no musical time (a radio beaconing, a stack advertising).
  // It lives here rather than being read from a clock inside the renderer
  // so render_ui() stays pure and the golden tests stay meaningful.
  uint32_t anim_tick = 0;
};

// Encoder-driven menu state machine (pure logic; host-tested).
//
// The tree is deliberately shallow (DESIGN_SYSTEM.md §11): one menu with
// six destinations plus BACK, at most one level below it, and long-press
// as the universal way back.
//
//   Home --click--> Menu [Live, Outputs, Network, MIDI, Audio, System, Back]
//     Outputs [CLK1..4] --click--> OutputEdit (param list)
//     Network              read-only; credentials are the web UI's job
//     MIDI, Audio, System  param lists
//     System > REBOOT   --click--> Confirm
//
// Interaction (§11): rotate moves focus or changes the focused value
// (on the live screen it queues a whole-BPM nudge via take_tempo_nudge),
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
    kAudio,
    kSystem,
    kConfirm,
  };

  // Requests the model cannot carry out itself. The owner polls
  // take_action() and performs the platform-specific part.
  enum class Action : uint8_t {
    kNone,
    kReboot,
  };

  static constexpr int kMenuItems = 7;  // Live, Outputs, Network, MIDI, Audio, System, Back
  static constexpr int kMenuBackItem = kMenuItems - 1;
  static constexpr int kOutputsItems = 4;  // CLK1..4
  // ENABLED, PPQN, MULT, DIV, MODE, TRIG MS, DUTY, SHUF, then the parity
  // set: ROLE, FREE RUN, RHYTHM, STEPS, FILLS, ROT, CHANCE, JITTER,
  // PER LOOP. The shape parameters stay first because they are what a
  // user reaches for at the rack; the pattern editor lives in the web UI.
  static constexpr int kOutputEditItems = 17;
  static constexpr int kMidiItems = 5;
  // AUDIO, METRO, CLICK, SOUND, OUT L, OUT R, LINE IN, PUBLISH, SUB.
  // Subscribing from the panel cycles the channels Link Audio discovered;
  // naming one is the web editor's job, where there is a keyboard.
  static constexpr int kAudioItems = 9;
  // LATENCY, RESET, SOURCE, IN PPQN, GATE CLK, QUANTUM, RST EDGE,
  // MIDI NDG, SS SYNC, BRIGHT, BEAT, STYLE, COLOUR, VERSION, REBOOT —
  // REBOOT last.
  static constexpr int kSystemItems = 15;
  // Read-only: the firmware version string, filled in by the renderer from
  // UiStatus rather than by item_value() (MenuModel has no platform code to
  // read esp_app_desc_t from). on_click() must not toggle editing_ for it.
  static constexpr int kSystemVersionItem = kSystemItems - 2;
  static constexpr int kSystemRebootItem = kSystemItems - 1;

  explicit MenuModel(Config* cfg) : cfg_(cfg) {}

  void on_rotate(int detents);
  void on_click();
  // Cancels an in-progress edit if there is one, otherwise goes back one
  // level. From the menu this returns to the live screen.
  void on_long_press();
  void go_home();
  // Touch / direct navigation: jump to a section, pick a row, or nudge
  // the focused value without an encoder click into edit mode.
  void go_section(Screen s);
  void set_cursor(int index);
  void set_output_index(int index);
  void set_confirm_yes(bool yes);
  void nudge_value(int delta);

  bool take_dirty();
  Action take_action();
  // Whole-BPM steps queued by rotating on the live screen. One-shot.
  int take_tempo_nudge();

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
  void adjust_audio(int index, int delta);
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
  int tempo_nudge_ = 0;
};

}  // namespace neon
