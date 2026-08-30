#include <doctest.h>

#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "neon/config/model.hpp"
#include "neon/gfx/rlcd_canvas.hpp"

using neon::MonoTheme;
using neon::RlcdCanvas;
using neon::RlcdPanelStatus;
using Orientation = neon::RlcdCanvas::Orientation;

TEST_CASE("logical extent follows orientation") {
  RlcdCanvas c;
  CHECK(c.width() == 400);
  CHECK(c.height() == 300);
  c.set_orientation(Orientation::kPortrait);
  CHECK(c.width() == 300);
  CHECK(c.height() == 400);
}

TEST_CASE("portrait maps logical coords into the physical buffer") {
  // Portrait logical (x, y) -> physical (kWidth-1-y, x). The mapping is a
  // bijection between the 300x400 logical plane and the 400x300 buffer, so
  // a pixel set in portrait reads back at the rotated spot in landscape.
  RlcdCanvas c;
  c.set_orientation(Orientation::kPortrait);
  c.set_pixel(0, 0, true);      // logical top-left
  c.set_pixel(299, 399, true);  // logical bottom-right
  CHECK(c.pixel(0, 0));         // round-trips in portrait
  CHECK(c.pixel(299, 399));

  c.set_orientation(Orientation::kLandscape);
  CHECK(c.pixel(399, 0));   // (0,0)     -> physical (399, 0)
  CHECK(c.pixel(0, 299));   // (299,399) -> physical (0, 299)
}

TEST_CASE("filling the whole logical area fills the whole buffer") {
  RlcdCanvas c;
  c.set_orientation(Orientation::kPortrait);
  c.fill_rect(0, 0, c.width(), c.height(), true);
  // 300*400 logical pixels == 400*300 physical pixels, all inked.
  CHECK(c.black_pixels() == 300 * 400);
}

TEST_CASE("out-of-range logical writes are clipped, not wrapped") {
  RlcdCanvas c;
  c.set_orientation(Orientation::kPortrait);
  c.set_pixel(-1, 10, true);
  c.set_pixel(300, 10, true);   // just past logical width
  c.set_pixel(10, 400, true);   // just past logical height
  CHECK(c.black_pixels() == 0);
}

TEST_CASE("both orientations render a non-empty status face") {
  RlcdPanelStatus rs{};
  rs.base.milli_bpm = 128000;
  rs.base.playing = 1;
  rs.quantum = 4;
  rs.beat = 2;
  rs.battery_pct = 70;
  std::snprintf(rs.base.title, sizeof(rs.base.title), "link-rlcd");

  RlcdCanvas land;
  neon::render_rlcd_panel(land, rs);
  CHECK(land.black_pixels() > 500);

  RlcdCanvas port;
  port.set_orientation(Orientation::kPortrait);
  neon::render_rlcd_panel(port, rs);
  CHECK(port.black_pixels() > 500);
}

namespace {

RlcdPanelStatus themed_status(MonoTheme t, bool playing) {
  RlcdPanelStatus rs{};
  rs.base.milli_bpm = 128000;
  rs.base.playing = playing;
  rs.base.peers = 3;
  rs.base.provisioned = true;
  rs.base.wifi_up = true;
  rs.quantum = 4;
  rs.beat = playing ? 2 : 0;
  rs.battery_pct = 70;
  rs.theme = static_cast<uint8_t>(t);
  rs.base.invert = neon::mono_theme_dark(t);
  std::snprintf(rs.base.title, sizeof(rs.base.title), "link-rlcd");
  return rs;
}

}  // namespace

TEST_CASE("every theme renders a non-empty face in both orientations") {
  for (int t = 0; t < static_cast<int>(MonoTheme::kCount); ++t) {
    const RlcdPanelStatus rs = themed_status(static_cast<MonoTheme>(t), true);
    RlcdCanvas land;
    neon::render_rlcd_panel(land, rs);
    const int total = RlcdCanvas::kWidth * RlcdCanvas::kHeight;
    CHECK(land.black_pixels() > 500);
    CHECK(land.black_pixels() < total - 500);

    RlcdCanvas port;
    port.set_orientation(Orientation::kPortrait);
    neon::render_rlcd_panel(port, rs);
    CHECK(port.black_pixels() > 500);
    CHECK(port.black_pixels() < total - 500);
  }
}

TEST_CASE("themes actually change the pixels on the glass") {
  RlcdCanvas classic;
  neon::render_rlcd_panel(classic,
                          themed_status(MonoTheme::kClassic, true));
  for (int t = 1; t < static_cast<int>(MonoTheme::kCount); ++t) {
    RlcdCanvas c;
    neon::render_rlcd_panel(c, themed_status(static_cast<MonoTheme>(t), true));
    CHECK(std::memcmp(c.data(), classic.data(), RlcdCanvas::kSize) != 0);
  }
}

TEST_CASE("dark themes come out mostly ink, light themes mostly paper") {
  const int total = RlcdCanvas::kWidth * RlcdCanvas::kHeight;
  {
    RlcdCanvas c;
    neon::render_rlcd_panel(c, themed_status(MonoTheme::kHero, true));
    CHECK(c.black_pixels() > total / 2);
  }
  {
    RlcdCanvas c;
    neon::render_rlcd_panel(c, themed_status(MonoTheme::kInk, true));
    CHECK(c.black_pixels() < total / 2);
  }
}

TEST_CASE("NIGHT is the classic face inverted") {
  RlcdCanvas classic;
  neon::render_rlcd_panel(classic,
                          themed_status(MonoTheme::kClassic, true));
  RlcdCanvas night;
  neon::render_rlcd_panel(night, themed_status(MonoTheme::kNight, true));
  const int total = RlcdCanvas::kWidth * RlcdCanvas::kHeight;
  CHECK(night.black_pixels() == total - classic.black_pixels());
}

TEST_CASE("PULSE flashes inverted on the downbeat") {
  RlcdPanelStatus rs = themed_status(MonoTheme::kPulse, true);
  const int total = RlcdCanvas::kWidth * RlcdCanvas::kHeight;
  rs.beat = 1;
  RlcdCanvas accent;
  neon::render_rlcd_panel(accent, rs);
  CHECK(accent.black_pixels() > total / 2);
  rs.beat = 2;
  RlcdCanvas offbeat;
  neon::render_rlcd_panel(offbeat, rs);
  CHECK(offbeat.black_pixels() < total / 2);
}

TEST_CASE("minimal faces still print setup AP credentials") {
  for (MonoTheme t : {MonoTheme::kInk, MonoTheme::kDots, MonoTheme::kHero,
                      MonoTheme::kConsole, MonoTheme::kGrid,
                      MonoTheme::kPulse}) {
    RlcdPanelStatus rs = themed_status(t, false);
    RlcdCanvas without;
    neon::render_rlcd_panel(without, rs);

    rs.base.setup_ap = true;
    std::snprintf(rs.base.ap_ssid, sizeof(rs.base.ap_ssid), "NEON-LINK-1234");
    std::snprintf(rs.base.ap_pass, sizeof(rs.base.ap_pass), "link-ABC123");
    RlcdCanvas with;
    neon::render_rlcd_panel(with, rs);
    CHECK(std::memcmp(with.data(), without.data(), RlcdCanvas::kSize) != 0);
  }
}

TEST_CASE("menu overlay draws all nine settings rows") {
  RlcdPanelStatus rs{};
  rs.base.milli_bpm = 120000;
  rs.base.overlay = 1;
  rs.base.n_items = 9;
  for (int i = 0; i < 9; ++i) {
    std::snprintf(rs.base.item_label[i], sizeof(rs.base.item_label[i]),
                  "ROW %d", i);
    std::snprintf(rs.base.item_value[i], sizeof(rs.base.item_value[i]), "V%d",
                  i);
  }
  rs.base.cursor = 8;
  for (Orientation o : {Orientation::kLandscape, Orientation::kPortrait}) {
    RlcdCanvas with;
    with.set_orientation(o);
    neon::render_rlcd_panel(with, rs);

    RlcdPanelStatus fewer = rs;
    fewer.base.n_items = 8;
    fewer.base.cursor = 0;
    RlcdCanvas without;
    without.set_orientation(o);
    neon::render_rlcd_panel(without, fewer);
    CHECK(std::memcmp(with.data(), without.data(), RlcdCanvas::kSize) != 0);
  }
}
