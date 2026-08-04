#include <doctest.h>

#include "neon/net/preference.hpp"

using neon::ActiveNet;
using neon::NetPreference;

TEST_CASE("ethernet is preferred over wifi whenever it has an address") {
  NetPreference p;
  CHECK(p.active() == ActiveNet::kNone);

  p.wifi_configured(true);
  p.wifi_ip(true);
  CHECK(p.active() == ActiveNet::kWifi);

  p.eth_link(true);
  CHECK(p.active() == ActiveNet::kWifi);  // link up but no address yet
  p.eth_ip(true);
  CHECK(p.active() == ActiveNet::kEthernet);

  // Cable pulled: instant fallback to WiFi.
  p.eth_link(false);
  CHECK(p.active() == ActiveNet::kWifi);
}

TEST_CASE("eth address is ignored unless the link is up") {
  NetPreference p;
  p.eth_ip(true);  // stale event without link
  CHECK(p.active() == ActiveNet::kNone);
}

TEST_CASE("AP recommended after short grace when wifi is unconfigured") {
  NetPreference p;
  p.wifi_configured(false);
  CHECK_FALSE(p.update_should_start_ap(0));
  CHECK_FALSE(p.update_should_start_ap(9 * 1000000ll));
  CHECK(p.update_should_start_ap(10 * 1000000ll));
}

TEST_CASE("AP grace is long when wifi is configured but down") {
  NetPreference p;
  p.wifi_configured(true);
  CHECK_FALSE(p.update_should_start_ap(0));
  CHECK_FALSE(p.update_should_start_ap(30 * 1000000ll));
  CHECK(p.update_should_start_ap(61 * 1000000ll));
}

TEST_CASE("connectivity resets the AP grace timer") {
  NetPreference p;
  p.wifi_configured(false);
  CHECK_FALSE(p.update_should_start_ap(0));
  // Ethernet comes up at t=5s; timer resets.
  p.eth_link(true);
  p.eth_ip(true);
  CHECK_FALSE(p.update_should_start_ap(5 * 1000000ll));
  // Down again at t=20s: grace restarts from there.
  p.eth_link(false);
  CHECK_FALSE(p.update_should_start_ap(20 * 1000000ll));
  CHECK_FALSE(p.update_should_start_ap(29 * 1000000ll));
  CHECK(p.update_should_start_ap(30 * 1000000ll));
}

TEST_CASE("no AP while any interface holds an address") {
  NetPreference p;
  p.wifi_configured(true);
  p.wifi_ip(true);
  CHECK_FALSE(p.update_should_start_ap(1000 * 1000000ll));
  p.wifi_ip(false);
  p.eth_link(true);
  p.eth_ip(true);
  CHECK_FALSE(p.update_should_start_ap(2000 * 1000000ll));
}
