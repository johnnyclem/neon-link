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

// Where the animated icons are in their loops.
//
// Beat-locked loops freeze at frame 0 while the transport is stopped. The
// Link timeline keeps advancing whether or not anything is playing, so
// without this the run gate would march and the play triangle would count
// a bar that is not happening.
IconClocks icon_clocks(const UiStatus& s) {
  IconClocks clocks;
  clocks.tick = s.anim_tick;
  if (s.playing) {
    clocks.beat = s.phase_milli_beats / 1000;
  }
  return clocks;
}

// ---- screens ------------------------------------------------------------

void render_home(const UiStatus& s, Framebuffer& fb, const Layout& lay) {
  if (s.playing && s.big_beat_display) {
    draw_beat_stage(fb, s.phase_milli_beats, s.quantum_beats, s.beat_style,
                    lay.height);
    return;
  }

  // Link first: a ring pulsing outward on every beat is the panel's "this
  // session is alive, and here is how fast" indicator, and it is the one
  // place the tempo shows up as motion rather than as a number. Dropped
  // when an external clock is driving, where the word EXT carries it.
  const Icon* icons[3] = {s.ext_clock ? nullptr : &kIconLink, &net_icon(s),
                          s.ble_on ? &kIconBle : nullptr};
  draw_header(fb, kBrand, icons, 3, icon_clocks(s));

  draw_hero_bpm(fb, s.milli_bpm, s.tempo_valid, lay.hero_y);
  if (lay.unit_y >= 0) {
    draw_label(fb, kAlignPanel, lay.unit_y, "BPM", Align::kCenter);
  }

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
  draw_status_row(fb, lay.status_y, words, 4);

  // The identity row is one of the bands the compact flow drops.
  // Setup AP: put the password on the live screen. NETWORK buries it
  // below four readout rows and people miss it; joining this AP is
  // how a friend gets on the box at all.
  if (lay.ident_y >= 0) {
    draw_label(fb, kAlignPanel, lay.ident_y, editor_address(s),
               Align::kCenter);
    if (s.setup_ap && s.ap_pass[0] != '\0') {
      draw_label(fb, kAlignPanel, lay.ident_y + 10, s.ap_pass, Align::kCenter);
    }
  }

  // Deliberate empty band between the identity line and the phase bar. A
  // 128x128 display rewards restraint (DESIGN_SYSTEM.md §3.2).
  draw_bar(fb, lay.bar_y, s.phase_milli_beats, s.quantum_beats, s.playing,
           lay.bar_h, lay.bar_tick_h);
}

void render_list(const MenuModel& menu, const char* title,
                 const UiStatus& status, Framebuffer& fb, const Layout& lay) {
  draw_header(fb, title);

  const int n = menu.item_count();
  int first = 0;
  if (menu.cursor() >= lay.list_rows) {
    first = menu.cursor() - lay.list_rows + 1;
  }
  for (int row = 0; row < lay.list_rows; ++row) {
    const int idx = first + row;
    if (idx >= n) {
      break;
    }
    const bool focused = idx == menu.cursor();
    char value[16];
    // The one row MenuModel cannot fill in itself: it is pure host-tested
    // logic with no way to read esp_app_desc_t, so the platform's firmware
    // string comes in through UiStatus instead of item_value().
    if (menu.screen() == MenuModel::Screen::kSystem &&
        idx == MenuModel::kSystemVersionItem) {
      // Explicit precision, not "%s": status.firmware can be longer than
      // this row's value column, and an unbounded "%s" into a smaller
      // buffer is exactly the truncation GCC's -Wformat-truncation (built
      // as -Werror here) exists to catch.
      std::snprintf(value, sizeof(value), "%.*s",
                    static_cast<int>(sizeof(value) - 1), status.firmware);
    } else {
      menu.item_value(idx, value, sizeof(value));
    }
    draw_list_row(fb, row, menu.item_label(idx), value, focused,
                  focused && menu.editing(), lay.list_top, lay.list_row_h);
  }
}

// Read-only. Credentials are the web UI's job — there is no keyboard here.
void render_network(const UiStatus& s, Framebuffer& fb, const Layout& lay) {
  draw_header(fb, kTitleNetwork);

  // Wide enough for any uint32_t: this screen is a readout, so it shows the
  // real count rather than the live screen's abbreviated one.
  char peers[12];
  std::snprintf(peers, sizeof(peers), "%u", static_cast<unsigned>(s.peers));

  draw_list_row(fb, 0, "MODE", net_word(s), false, false, lay.list_top,
                lay.list_row_h);
  int row = 1;
  if (s.setup_ap && s.ap_pass[0] != '\0') {
    draw_list_row(fb, row++, "AP PASS", s.ap_pass, false, false, lay.list_top,
                  lay.list_row_h);
  }
  draw_list_row(fb, row++, "IP", editor_address(s), false, false, lay.list_top,
                lay.list_row_h);
  draw_list_row(fb, row++, "PEERS", peers, false, false, lay.list_top,
                lay.list_row_h);
  draw_list_row(fb, row++, "SOURCE", source_word(s), false, false, lay.list_top,
                lay.list_row_h);
  if (s.device_token[0] != '\0') {
    // The full 32-char token does not fit a list row's value column, and
    // asking someone to read that many hex digits over the phone is not
    // reasonable anyway — an 8-char prefix is enough to disambiguate a
    // support call ("does the code on your panel start with...") without
    // being the whole secret.
    char short_token[9];
    std::snprintf(short_token, sizeof(short_token), "%.*s",
                  static_cast<int>(sizeof(short_token) - 1), s.device_token);
    draw_list_row(fb, row++, "TOKEN", short_token, false, false,
                  lay.list_top, lay.list_row_h);
  }

  // The setup pointer rides below the identity band, which the compact
  // flow does not have; its four readout rows already fill that panel.
  if (lay.ident_y >= 0) {
    draw_label(fb, kAlignPanel, lay.ident_y + 12, "EDIT ON THE WEB",
               Align::kCenter);
    draw_label(fb, kAlignPanel, lay.ident_y + 24, kSetupIp, Align::kCenter);
  }
}

void render_output_edit(const MenuModel& menu, const UiStatus& status,
                        Framebuffer& fb, const Layout& lay) {
  char title[16];
  std::snprintf(title, sizeof(title), "CLK %d", (menu.output_index() & 3) + 1);
  render_list(menu, title, status, fb, lay);
}

}  // namespace

void render_ui(const MenuModel& menu, const UiStatus& status,
               Framebuffer& fb, const ui::Layout& layout) {
  fb.clear();
  switch (menu.screen()) {
    case MenuModel::Screen::kHome:
      render_home(status, fb, layout);
      break;
    case MenuModel::Screen::kMenu:
    case MenuModel::Screen::kOutputs:
    case MenuModel::Screen::kMidi:
    case MenuModel::Screen::kAudio:
    case MenuModel::Screen::kSystem:
      render_list(menu, menu.screen_title(), status, fb, layout);
      break;
    case MenuModel::Screen::kOutputEdit:
      render_output_edit(menu, status, fb, layout);
      break;
    case MenuModel::Screen::kNetwork:
      render_network(status, fb, layout);
      break;
    case MenuModel::Screen::kConfirm:
      neon::ui::draw_confirm(fb, menu.screen_title(), "REBOOT THE",
                             "MODULE NOW?", menu.confirm_yes(), layout);
      break;
  }
}

void render_ui(const MenuModel& menu, const UiStatus& status,
               Framebuffer& fb) {
  render_ui(menu, status, fb, ui::kLayout128);
}

}  // namespace neon
