#include <doctest.h>

#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#include "neon/client/bind.hpp"
#include "neon/client/status.hpp"

namespace {

std::string load(const char* name) {
#ifdef NEON_CLIENT_FIXTURES
  const std::string path = std::string(NEON_CLIENT_FIXTURES) + "/" + name;
#else
  const std::string path = std::string("plugin/client/tests/fixtures/") + name;
#endif
  std::ifstream in(path);
  REQUIRE(in.good());
  std::ostringstream os;
  os << in.rdbuf();
  return os.str();
}

}  // namespace

TEST_CASE("parse firmware-shaped status; bind reconnect is stage-left.local") {
  const std::string json = load("status_firmware.json");
  neon::client::Status s;
  REQUIRE(neon::client::parse_status(json.c_str(), json.size(), &s));
  CHECK(s.device_name == "stage-left");
  CHECK(s.hostname == "stage-left.local");
  CHECK(s.rev == 7);
  CHECK_FALSE(s.persist_lazy);
  CHECK(s.bpm == doctest::Approx(128.0));
  CHECK(s.playing);
  CHECK(s.network == neon::client::NetworkKind::Wifi);
  CHECK(s.pulse.late_max_us == 184);
  CHECK(s.audio.rx_dropped == 0);

  const std::string reconnect = neon::client::mdns_host(s.device_name);
  CHECK(reconnect == "stage-left.local");
  CHECK(reconnect.find(".local.local") == std::string::npos);
}

TEST_CASE("pre-F5 status: persist_lazy and rev default to false/0") {
  const std::string json = load("status_pre_f5.json");
  neon::client::Status s;
  REQUIRE(neon::client::parse_status(json.c_str(), json.size(), &s));
  CHECK_FALSE(s.persist_lazy);
  CHECK(s.rev == 0);
  CHECK(s.firmware == "0.0.9");
}

TEST_CASE("persist_lazy present") {
  const char* json =
      "{\"bpm\":120,\"persist_lazy\":true,\"device_name\":\"n\","
      "\"hostname\":\"n.local\"}";
  neon::client::Status s;
  REQUIRE(neon::client::parse_status(json, std::strlen(json), &s));
  CHECK(s.persist_lazy);
}

TEST_CASE("malformed status is rejected") {
  neon::client::Status s;
  CHECK_FALSE(neon::client::parse_status("not json", 8, &s));
  CHECK_FALSE(neon::client::parse_status("[1,2]", 5, &s));
}
