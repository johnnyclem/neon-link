// Renders the real device screens on the host and writes design/screens.json.
//
// The point is reviewability: the panel's design can be looked at, argued
// about and diffed without flashing hardware, and the style guide can show a
// screen next to the web component that mirrors it. Every fixture here goes
// through the same render_ui() the firmware calls, so what the simulator
// shows is what the panel draws — not a redrawn approximation.
//
//   cmake --build build-host --target neon_screens
//   ./build-host/neon_screens > design/screens.json
//
// Output is the raw 2048-byte framebuffer per screen, base64'd. That is the
// exact byte layout the SH1107 receives (16 pages x 128 columns, one byte =
// 8 vertical pixels, LSB on top), so the simulator unpacks the panel's own
// format rather than a convenience encoding that could drift from it.

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "neon/config/model.hpp"
#include "neon/gfx/framebuffer.hpp"
#include "neon/ui/menu_model.hpp"
#include "neon/ui/render.hpp"

namespace {

std::string base64(const uint8_t* data, size_t len) {
  static const char* kAlphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((len + 2) / 3 * 4);
  for (size_t i = 0; i < len; i += 3) {
    const uint32_t a = data[i];
    const uint32_t b = i + 1 < len ? data[i + 1] : 0;
    const uint32_t c = i + 2 < len ? data[i + 2] : 0;
    const uint32_t triple = (a << 16) | (b << 8) | c;
    out += kAlphabet[(triple >> 18) & 0x3f];
    out += kAlphabet[(triple >> 12) & 0x3f];
    out += i + 1 < len ? kAlphabet[(triple >> 6) & 0x3f] : '=';
    out += i + 2 < len ? kAlphabet[triple & 0x3f] : '=';
  }
  return out;
}

std::string escape(const std::string& s) {
  std::string out;
  for (char c : s) {
    if (c == '"' || c == '\\') {
      out += '\\';
    }
    out += c;
  }
  return out;
}

struct Fixture {
  std::string id;
  std::string title;
  std::string note;
  std::string data;
};

neon::UiStatus base_status() {
  neon::UiStatus s;
  s.milli_bpm = 120000;
  s.quantum_beats = 4;
  s.phase_milli_beats = 1500;
  s.tempo_valid = true;
  return s;
}

// Drives the menu to a screen the way a user's encoder would, so the
// fixtures exercise the real navigation graph rather than poking at state.
void navigate(neon::MenuModel& m, int menu_index, int descend_clicks = 0) {
  m.on_click();  // Home -> Menu
  m.on_rotate(menu_index);
  m.on_click();
  for (int i = 0; i < descend_clicks; ++i) {
    m.on_click();
  }
}

}  // namespace

int main() {
  std::vector<Fixture> fixtures;
  std::vector<Fixture> fixtures64;

  // Every fixture is rendered twice: once with the full 128x128 layout
  // and once with the compact 128x64 one, so a layout change is reviewed
  // on both geometries in the same diff. The compact flow only ever
  // touches the top half of the framebuffer (that property is what makes
  // a 64-row panel's flush pages 0-7 verbatim), so its capture is the
  // first 8 pages.
  auto capture = [&](const std::string& id, const std::string& title,
                     const std::string& note, const neon::MenuModel& menu,
                     const neon::UiStatus& status) {
    neon::Framebuffer fb;
    neon::render_ui(menu, status, fb);
    fixtures.push_back(
        {id, title, note, base64(fb.data(), neon::Framebuffer::kSize)});

    neon::Framebuffer fb64;
    neon::render_ui(menu, status, fb64, neon::ui::kLayout64);
    fixtures64.push_back({id, title, note,
                          base64(fb64.data(), neon::Framebuffer::kWidth *
                                                  neon::ui::kLayout64.height /
                                                  8)});
  };

  // ---- live screen, through its real states --------------------------
  {
    neon::Config cfg;
    neon::MenuModel m(&cfg);

    neon::UiStatus boot = base_status();
    boot.tempo_valid = false;
    boot.setup_ap = true;
    boot.playing = false;
    boot.phase_milli_beats = 0;
    capture("live-boot", "Live — first boot",
            "No Link session yet: the hero shows its placeholder rather than a "
            "tempo the module is not running at.",
            m, boot);

    neon::UiStatus ap = base_status();
    ap.setup_ap = true;
    ap.playing = true;
    capture("live-ap", "Live — setup access point",
            "The strongest moment of cohesion: the address on the panel is the "
            "address you open the web UI at.",
            m, ap);

    neon::UiStatus linked = base_status();
    linked.milli_bpm = 128000;
    linked.peers = 2;
    linked.playing = true;
    linked.active_net = 2;
    linked.ble_on = true;
    std::snprintf(linked.ip, sizeof(linked.ip), "10.0.0.42");
    capture("live-linked", "Live — running, two peers",
            "While playing the panel fills with the current beat. Phase 1.5 "
            "of a 4-beat bar is beat 2: black numeral on a white field.",
            m, linked);

    for (uint32_t beat = 1; beat <= 4; ++beat) {
      neon::UiStatus b = linked;
      b.phase_milli_beats = (beat - 1) * 1000;
      char id[16];
      char title[24];
      std::snprintf(id, sizeof(id), "live-beat-%u",
                    static_cast<unsigned>(beat));
      std::snprintf(title, sizeof(title), "Live — beat %u",
                    static_cast<unsigned>(beat));
      capture(id, title,
              beat % 2 == 1
                  ? "Odd beat: largest white numeral inside a 2 px black border."
                  : "Even beat: the panel inverts — black numeral, white field.",
              m, b);
    }

    neon::UiStatus classic = linked;
    classic.big_beat_display = false;
    capture("live-classic", "Live — running, classic layout",
            "Big beat numbers can be turned off; the BPM home screen returns.",
            m, classic);

    // The header's animated icons, one fixture per beat. Read left to right
    // and the Link ring radiates outward on the beat while the Wi-Fi and BLE
    // marks run on their own fixed-rate clock. Captured because a loop that
    // is only ever seen in a still frame is a loop nobody reviews.
    for (uint32_t beat = 0; beat < 4; ++beat) {
      neon::UiStatus a = classic;
      a.phase_milli_beats = beat * 1000;
      a.anim_tick = beat;
      char id[24];
      char title[32];
      std::snprintf(id, sizeof(id), "header-beat-%u",
                    static_cast<unsigned>(beat + 1));
      std::snprintf(title, sizeof(title), "Header — beat %u",
                    static_cast<unsigned>(beat + 1));
      capture(id, title,
              "Link pulses on the beat; the radio marks run on the 5 Hz tick.",
              m, a);
    }

    neon::UiStatus stopped = linked;
    stopped.playing = false;
    stopped.phase_milli_beats = 2600;
    capture("live-stopped", "Live — stopped",
            "The phase fill goes half-tone when the transport is stopped, so a "
            "held position is visibly not advancing.",
            m, stopped);

    neon::UiStatus ext = linked;
    ext.ext_clock = true;
    capture("live-external", "Live — external clock",
            "External clock drives the session; the source word changes and the "
            "rest of the layout holds still.",
            m, ext);

    // The only screen that shows the warning icon. Without a fixture for it
    // the icon was only ever reviewed in the style guide's icon grid, which
    // is how a badly-drawn one survived — a hazard sign has to be judged at
    // 8 px in the header, not at 32 px on its own.
    neon::UiStatus offline = base_status();
    offline.big_beat_display = false;
    offline.active_net = 0;
    offline.setup_ap = false;
    capture("live-offline", "Live — no network",
            "No access point and no network joined: the header carries the "
            "warning icon and the state row reads OFF.",
            m, offline);
  }

  // ---- menus ----------------------------------------------------------
  {
    neon::UiStatus st = base_status();
    st.peers = 2;
    st.playing = true;
    st.active_net = 2;
    st.ble_on = true;
    std::snprintf(st.ip, sizeof(st.ip), "10.0.0.42");

    neon::Config cfg;
    neon::MenuModel menu(&cfg);
    menu.on_click();
    capture("menu", "Menu",
            "Five destinations, one level deep. Long press is the way back "
            "from anywhere.",
            menu, st);

    neon::Config outputs_cfg;
    neon::MenuModel outputs(&outputs_cfg);
    navigate(outputs, 1);
    capture("outputs", "Outputs", "The four clock outputs.", outputs, st);

    neon::Config edit_cfg;
    neon::MenuModel browsing(&edit_cfg);
    navigate(browsing, 1, 1);
    capture("clk-browse", "Clock 1 — browsing",
            "A caret marks focus while the encoder is moving the cursor.",
            browsing, st);

    neon::Config editing_cfg;
    neon::MenuModel editing(&editing_cfg);
    navigate(editing, 1, 1);
    editing.on_rotate(1);
    editing.on_click();
    capture("clk-edit", "Clock 1 — editing PPQN",
            "The row inverts while the encoder is changing a value, so the "
            "knob's mode is never ambiguous.",
            editing, st);

    neon::Config net_cfg;
    neon::MenuModel network(&net_cfg);
    navigate(network, 2);
    capture("network", "Network",
            "Read-only. Credentials need a keyboard, so they live in the web "
            "UI and the panel points you at it.",
            network, st);

    neon::Config midi_cfg;
    neon::MenuModel midi(&midi_cfg);
    navigate(midi, 3);
    capture("midi", "MIDI / BLE", "Routing that is worth reaching without a phone.",
            midi, st);

    neon::Config audio_cfg;
    audio_cfg.audio.enabled = 1;
    audio_cfg.audio.metro_enabled = 1;
    neon::MenuModel audio(&audio_cfg);
    navigate(audio, 4);
    capture("audio", "Audio",
            "The metronome, what each output jack carries, and whether the "
            "mix is published over Link Audio.",
            audio, st);

    neon::Config sys_cfg;
    neon::MenuModel system(&sys_cfg);
    navigate(system, 5);
    capture("system", "System", "Timing offsets, clock source and reboot.",
            system, st);

    neon::Config confirm_cfg;
    neon::MenuModel confirm(&confirm_cfg);
    navigate(confirm, 5);
    confirm.on_rotate(neon::MenuModel::kSystemRebootItem);
    confirm.on_click();
    capture("confirm-no", "Confirm — default",
            "Destructive actions get a confirmation screen, defaulting to NO.",
            confirm, st);
    confirm.on_rotate(1);
    capture("confirm-yes", "Confirm — YES selected",
            "The selected choice is inverted; there is no colour to fall back "
            "on here.",
            confirm, st);
  }

  // ---- emit -----------------------------------------------------------
  std::printf("{\n");
  std::printf("  \"$comment\": \"GENERATED FILE - do not edit. Rendered by ");
  std::printf("host/sim/neon_screens.cpp through the firmware's own ");
  std::printf("render_ui(). Regenerate: cmake --build build-host --target ");
  std::printf("neon_screens && ./build-host/neon_screens > design/screens.json\",\n");
  std::printf("  \"width\": %d,\n", neon::Framebuffer::kWidth);
  std::printf("  \"height\": %d,\n", neon::Framebuffer::kHeight);
  std::printf(
      "  \"format\": \"base64 of the raw framebuffer: 16 pages x 128 columns, "
      "one byte = 8 vertical pixels, LSB is the topmost row\",\n");
  auto emit_screens = [](const std::vector<Fixture>& list, const char* indent) {
    for (size_t i = 0; i < list.size(); ++i) {
      const Fixture& f = list[i];
      std::printf("%s{\n", indent);
      std::printf("%s  \"id\": \"%s\",\n", indent, escape(f.id).c_str());
      std::printf("%s  \"title\": \"%s\",\n", indent,
                  escape(f.title).c_str());
      std::printf("%s  \"note\": \"%s\",\n", indent, escape(f.note).c_str());
      std::printf("%s  \"bits\": \"%s\"\n", indent, f.data.c_str());
      std::printf("%s}%s\n", indent, i + 1 < list.size() ? "," : "");
    }
  };
  std::printf("  \"screens\": [\n");
  emit_screens(fixtures, "    ");
  std::printf("  ],\n");
  std::printf("  \"compact\": {\n");
  std::printf("    \"$comment\": \"The same fixtures through the compact ");
  std::printf("128x64 layout (ui::kLayout64) - native SSD1306/1309 panels. ");
  std::printf("8 pages x 128 columns, same packing.\",\n");
  std::printf("    \"width\": %d,\n", neon::Framebuffer::kWidth);
  std::printf("    \"height\": %d,\n", neon::ui::kLayout64.height);
  std::printf("    \"screens\": [\n");
  emit_screens(fixtures64, "      ");
  std::printf("    ]\n");
  std::printf("  }\n");
  std::printf("}\n");
  return 0;
}
