#include <doctest.h>

#include <cstdio>

#include "neon/gfx/rlcd_canvas.hpp"

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
