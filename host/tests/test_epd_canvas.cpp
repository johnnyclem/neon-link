#include <doctest.h>

#include <cstdio>
#include <cstring>

#include "neon/gfx/epd_canvas.hpp"

TEST_CASE("e-paper canvas is white until ink is drawn") {
  neon::EpdCanvas c;
  CHECK(c.black_pixels() == 0);
  c.set_pixel(0, 0, true);
  CHECK(c.black_pixels() == 1);
  CHECK(c.pixel(0, 0));
  CHECK_FALSE(c.pixel(1, 0));
}

TEST_CASE("link-sync panel draws BPM and PLAYING in ink") {
  neon::EpdCanvas c;
  neon::LinkSyncPanelStatus s;
  s.milli_bpm = 128000;
  s.playing = true;
  s.peers = 3;
  s.provisioned = true;
  s.wifi_up = true;
  std::snprintf(s.ssid, sizeof(s.ssid), "studio");
  neon::render_linksync_panel(c, s);
  CHECK(c.black_pixels() > 200);

  neon::EpdCanvas stopped;
  s.playing = false;
  neon::render_linksync_panel(stopped, s);
  CHECK(c.black_pixels() != stopped.black_pixels());
}

TEST_CASE("setup AP paints the password; joined network hides it") {
  neon::LinkSyncPanelStatus s;
  s.milli_bpm = 120000;
  s.setup_ap = true;
  s.provisioned = false;
  std::snprintf(s.ap_ssid, sizeof(s.ap_ssid), "LINK-EPD-C4A8");
  std::snprintf(s.ap_pass, sizeof(s.ap_pass), "link-55C4A8");
  std::snprintf(s.detail, sizeof(s.detail), "http://192.168.4.1");
  neon::EpdCanvas setup;
  neon::render_linksync_panel(setup, s);

  neon::LinkSyncPanelStatus joined = s;
  joined.setup_ap = false;
  joined.provisioned = true;
  joined.wifi_up = true;
  std::snprintf(joined.ssid, sizeof(joined.ssid), "studio");
  joined.ap_pass[0] = '\0';
  joined.detail[0] = '\0';
  neon::EpdCanvas lan;
  neon::render_linksync_panel(lan, joined);

  CHECK(std::memcmp(setup.data(), lan.data(), neon::EpdCanvas::kSize) != 0);
  CHECK(setup.black_pixels() > lan.black_pixels());
}

TEST_CASE("invert flips ink and menu overlay paints a highlight") {
  neon::LinkSyncPanelStatus s;
  s.milli_bpm = 120000;
  s.invert = true;
  s.overlay = 1;
  s.n_items = 2;
  std::snprintf(s.item_label[0], sizeof(s.item_label[0]), "PPQN");
  std::snprintf(s.item_value[0], sizeof(s.item_value[0]), "24");
  std::snprintf(s.item_label[1], sizeof(s.item_label[1]), "TRS");
  std::snprintf(s.item_value[1], sizeof(s.item_value[1]), "A");
  s.cursor = 1;
  neon::EpdCanvas c;
  neon::render_linksync_panel(c, s);
  CHECK(c.black_pixels() > 200);

  neon::LinkSyncPanelStatus live = s;
  live.invert = false;
  live.overlay = 0;
  neon::EpdCanvas d;
  neon::render_linksync_panel(d, live);
  CHECK(std::memcmp(c.data(), d.data(), neon::EpdCanvas::kSize) != 0);
}

TEST_CASE("splash wordmark and left-edge arrows") {
  neon::LinkSyncPanelStatus s;
  s.overlay = 4;
  neon::EpdCanvas c;
  neon::render_linksync_panel(c, s);
  CHECK(c.black_pixels() > 400);
  CHECK(c.pixel(12, neon::EpdCanvas::kHeight / 5));
  CHECK(c.pixel(12, (neon::EpdCanvas::kHeight * 4) / 5));
  CHECK_FALSE(c.pixel(400, 8));
}

TEST_CASE("e-paper panel is static text — same status, same pixels") {
  neon::LinkSyncPanelStatus s;
  s.milli_bpm = 88000;
  s.playing = true;
  s.peers = 1;
  s.provisioned = true;
  s.wifi_up = true;
  std::snprintf(s.ssid, sizeof(s.ssid), "clemhaus_IoT");
  neon::EpdCanvas a;
  neon::EpdCanvas b;
  neon::render_linksync_panel(a, s);
  neon::render_linksync_panel(b, s);
  CHECK(std::memcmp(a.data(), b.data(), neon::EpdCanvas::kSize) == 0);
}
