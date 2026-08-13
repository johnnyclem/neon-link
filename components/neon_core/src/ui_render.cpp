#include "neon/ui/render.hpp"

#include <cstdio>

#include "neon/timeline.hpp"
#include "neon/ui/widgets.hpp"

namespace neon {

namespace {

using namespace neon::ui;  // NOLINT(build/namespaces) — this file is the
                           // device's view layer; the theme is its vocabulary.

// ---- status vocabulary --------------------------------------------------
//
// Every word below comes from design/strings.json via theme_gen.hpp. The web
// UI renders the long form of the same entry, which is what stops the two
// surfaces drifting into synonyms for one state.

const char* source_word(const UiStatus& s) {
  return s.ext_clock ? kWordSourceExt : kWordSourceLink;
}

const char* transport_word(const UiStatus& s) {
  return s.playing ? kWordTransportRun : kWordTransportStop;
}

const char* net_word(const UiStatus& s) {
  if (s.setup_ap) {
    return kWordNetAp;
  }
  switch (s.active_net) {
    case 1:
      return kWordNetEthernet;
    case 2:
      return kWordNetWifi;
    default:
      return kWordNetNone;
  }
}

const Icon& net_icon(const UiStatus& s) {
  if (s.setup_ap) {
    return kIconWifiAp;
  }
  return s.active_net != 0 ? kIconWifiSta : kIconWarning;
}

// The address the editor is reachable at, or nothing when there isn't one.
const char* editor_address(const UiStatus& s) {
  if (s.ip[0] != '\0') {
    return s.ip;
  }
  return s.setup_ap ? kSetupIp : "";
}

// ---- screens ------------------------------------------------------------

void render_home(const UiStatus& s, Framebuffer& fb) {
  if (s.playing && s.big_beat_display) {
    draw_giant_beat(fb, neon::beat_number(s.phase_milli_beats, s.quantum_beats));
    return;
  }

  const Icon* icons[2] = {&net_icon(s), s.ble_on ? &kIconBle : nullptr};
  draw_header(fb, kBrand, icons, 2);

  draw_hero_bpm(fb, s.milli_bpm, s.tempo_valid);
  draw_label(fb, kAlignPanel, kUnitY, "BPM", Align::kCenter);

  // One line carrying all four pieces of machine state, in the same words
  // the web UI uses for its chips.
  char peers[8] = {};
  if (s.peers != 0) {
    // Two digits is what the status row has room for, and a session with
    // more peers than that has stopped being a number you read at a glance.
    const unsigned n = s.peers > 99 ? 99u : static_cast<unsigned>(s.peers);
    std::snprintf(peers, sizeof(peers), "%uP", n);
  }
  const char* words[4] = {source_word(s), transport_word(s), net_word(s),
                          peers};
  draw_status_row(fb, kStatusY, words, 4);

  draw_label(fb, kAlignPanel, kIdentY, editor_address(s), Align::kCenter);

  // Deliberate empty band between the identity line and the phase bar. A
  // 128x128 display rewards restraint (DESIGN_SYSTEM.md §3.2).
  draw_bar(fb, kBarY, s.phase_milli_beats, s.quantum_beats, s.playing);
}

void render_list(const MenuModel& menu, const char* title, Framebuffer& fb) {
  draw_header(fb, title);

  const int n = menu.item_count();
  int first = 0;
  if (menu.cursor() >= kListRows) {
    first = menu.cursor() - kListRows + 1;
  }
  for (int row = 0; row < kListRows; ++row) {
    const int idx = first + row;
    if (idx >= n) {
      break;
    }
    const bool focused = idx == menu.cursor();
    char value[16];
    menu.item_value(idx, value, sizeof(value));
    draw_list_row(fb, row, menu.item_label(idx), value, focused,
                  focused && menu.editing());
  }
}

// Read-only. Credentials are the web UI's job — there is no keyboard here.
void render_network(const UiStatus& s, Framebuffer& fb) {
  draw_header(fb, kTitleNetwork);

  // Wide enough for any uint32_t: this screen is a readout, so it shows the
  // real count rather than the live screen's abbreviated one.
  char peers[12];
  std::snprintf(peers, sizeof(peers), "%u", static_cast<unsigned>(s.peers));

  draw_list_row(fb, 0, "MODE", net_word(s), false, false);
  draw_list_row(fb, 1, "IP", editor_address(s), false, false);
  draw_list_row(fb, 2, "PEERS", peers, false, false);
  draw_list_row(fb, 3, "SOURCE", source_word(s), false, false);

  draw_label(fb, kAlignPanel, kIdentY + 12, "EDIT ON THE WEB", Align::kCenter);
  draw_label(fb, kAlignPanel, kIdentY + 24, kSetupIp, Align::kCenter);
}

void render_output_edit(const MenuModel& menu, Framebuffer& fb) {
  char title[16];
  std::snprintf(title, sizeof(title), "CLK %d", (menu.output_index() & 3) + 1);
  render_list(menu, title, fb);
}

}  // namespace

void render_ui(const MenuModel& menu, const UiStatus& status,
               Framebuffer& fb) {
  fb.clear();
  switch (menu.screen()) {
    case MenuModel::Screen::kHome:
      render_home(status, fb);
      break;
    case MenuModel::Screen::kMenu:
    case MenuModel::Screen::kOutputs:
    case MenuModel::Screen::kMidi:
    case MenuModel::Screen::kAudio:
    case MenuModel::Screen::kSystem:
      render_list(menu, menu.screen_title(), fb);
      break;
    case MenuModel::Screen::kOutputEdit:
      render_output_edit(menu, fb);
      break;
    case MenuModel::Screen::kNetwork:
      render_network(status, fb);
      break;
    case MenuModel::Screen::kConfirm:
      neon::ui::draw_confirm(fb, menu.screen_title(), "REBOOT THE",
                             "MODULE NOW?", menu.confirm_yes());
      break;
  }
}

}  // namespace neon
