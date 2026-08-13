#include <doctest.h>

#include "neon/client/bind.hpp"

TEST_CASE("dns_label strips a single trailing .local") {
  CHECK(neon::client::dns_label("stage-left") == "stage-left");
  CHECK(neon::client::dns_label("stage-left.local") == "stage-left");
  CHECK(neon::client::dns_label("stage-left.LOCAL") == "stage-left");
  CHECK(neon::client::dns_label("neon-link.local") == "neon-link");
}

TEST_CASE("mdns_host appends .local once") {
  CHECK(neon::client::mdns_host("stage-left") == "stage-left.local");
  CHECK(neon::client::mdns_host("stage-left.local") == "stage-left.local");
  CHECK(neon::client::mdns_host("") == "neon-link.local");
}

TEST_CASE("firmware hostname + device_name never double-suffix") {
  // Golden shape from handle_status: hostname already has .local.
  const std::string hostname = "stage-left.local";
  const std::string device_name = "stage-left";
  CHECK(neon::client::dns_label(hostname) == "stage-left");
  CHECK(neon::client::mdns_host(device_name) == "stage-left.local");
  CHECK(neon::client::mdns_host(neon::client::dns_label(hostname)) ==
        "stage-left.local");
}

TEST_CASE("ipv4 literal") {
  CHECK(neon::client::is_ipv4_literal("10.0.0.42"));
  CHECK(neon::client::is_ipv4_literal("192.168.4.1"));
  CHECK_FALSE(neon::client::is_ipv4_literal("stage-left.local"));
  CHECK_FALSE(neon::client::is_ipv4_literal("10.0.0"));
  CHECK_FALSE(neon::client::is_ipv4_literal("10.0.0.256"));
}
