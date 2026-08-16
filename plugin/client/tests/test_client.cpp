#include <doctest.h>

#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "neon/client/api.hpp"

namespace {

struct FakeHttp : neon::client::HttpTransport {
  struct Call {
    std::string method;
    std::string host;
    int port = 0;
    std::string path;
    std::string body;
    std::string extra_header;
  };
  std::vector<Call> calls;
  std::map<std::string, neon::client::HttpResponse> by_path;
  neon::client::HttpResponse fallback;

  neon::client::HttpResponse request(const char* method, const char* host,
                                     int port, const char* path,
                                     const char* body, int,
                                     const char* extra_header) override {
    Call c;
    c.method = method ? method : "";
    c.host = host ? host : "";
    c.port = port;
    c.path = path ? path : "";
    c.body = body ? body : "";
    c.extra_header = extra_header ? extra_header : "";
    calls.push_back(std::move(c));
    auto it = by_path.find(path ? path : "");
    if (it != by_path.end()) {
      return it->second;
    }
    return fallback;
  }
};

neon::client::HttpResponse ok(const std::string& body) {
  neon::client::HttpResponse r;
  r.status = 200;
  r.body = body;
  return r;
}

}  // namespace

TEST_CASE("putConfig Lazy refused when persist_lazy is absent") {
  FakeHttp http;
  http.by_path["/api/status"] = ok(
      "{\"bpm\":120,\"device_name\":\"n\",\"hostname\":\"n.local\","
      "\"firmware\":\"0.0.9\"}");
  neon::client::DeviceClient c(http);
  auto st = c.getStatus();
  REQUIRE(st.ok);
  CHECK_FALSE(c.persist_lazy());

  neon::client::JsonPatch p;
  p.setEngineLatency(-500);
  auto put = c.putConfig(p, neon::client::Persist::Lazy);
  CHECK_FALSE(put.ok);
  CHECK(put.error.find("persist_lazy") != std::string::npos);
  REQUIRE(http.calls.size() == 1);  // status only — no PUT
}

TEST_CASE("putConfig Lazy is sent when advertised") {
  FakeHttp http;
  http.by_path["/api/status"] = ok(
      "{\"bpm\":120,\"persist_lazy\":true,\"device_name\":\"n\","
      "\"hostname\":\"n.local\"}");
  http.by_path["/api/config?persist=lazy"] =
      ok("{\"engine\":{\"latency_us\":-500}}");
  neon::client::DeviceClient c(http);
  REQUIRE(c.getStatus().ok);
  CHECK(c.persist_lazy());

  neon::client::JsonPatch p;
  p.setEngineLatency(-500);
  auto put = c.putConfig(p, neon::client::Persist::Lazy);
  REQUIRE(put.ok);
  CHECK(put.value.engine.latency_us == -500);
  REQUIRE(http.calls.size() == 2);
  CHECK(http.calls[1].path == "/api/config?persist=lazy");
  CHECK(http.calls[1].body.find("\"latency_us\":-500") != std::string::npos);
}

TEST_CASE("putConfig Now always sends") {
  FakeHttp http;
  http.by_path["/api/config"] = ok("{\"engine\":{\"latency_us\":0}}");
  neon::client::DeviceClient c(http);
  neon::client::JsonPatch p;
  p.setEngineLatency(0);
  auto put = c.putConfig(p, neon::client::Persist::Now);
  REQUIRE(put.ok);
  CHECK(http.calls[0].path == "/api/config");
}

TEST_CASE("10 Hz latency ramp coalesces into N PUTs of one field") {
  FakeHttp http;
  http.fallback = ok("{\"engine\":{\"latency_us\":0}}");
  neon::client::DeviceClient c(http);
  for (int i = 0; i < 10; ++i) {
    neon::client::JsonPatch p;
    p.setEngineLatency(-100 * i);
    auto put = c.putConfig(p, neon::client::Persist::Now);
    REQUIRE(put.ok);
  }
  CHECK(http.calls.size() == 10);
  for (const auto& call : http.calls) {
    CHECK(call.path == "/api/config");
    CHECK(call.body.find("\"latency_us\"") != std::string::npos);
  }
}

TEST_CASE("transport and tempo paths") {
  FakeHttp http;
  http.fallback = ok("{\"ok\":true}");
  neon::client::DeviceClient c(http);
  REQUIRE(c.transport(neon::client::TransportOp::Toggle).ok);
  REQUIRE(c.tempoOp(neon::client::TempoOp::Nudge, -1).ok);
  REQUIRE(c.setTempo(128.5).ok);
  REQUIRE(c.preset(neon::client::PresetOp::Recall, 2).ok);
  CHECK(http.calls[0].path == "/api/transport?op=toggle");
  CHECK(http.calls[1].path == "/api/tempo?op=nudge&delta=-1");
  CHECK(http.calls[2].path.find("/api/tempo?bpm=") == 0);
  CHECK(http.calls[3].path == "/api/preset?op=recall&slot=2");
}

TEST_CASE("factoryReset treats a dropped connection as success") {
  FakeHttp http;
  neon::client::HttpResponse drop;
  drop.connect_failed = true;
  drop.error = "connection reset";
  http.by_path["/api/factory_reset?confirm=yes"] = drop;
  neon::client::DeviceClient c(http);
  auto r = c.factoryReset();
  CHECK(r.ok);
  REQUIRE_FALSE(http.calls.empty());
  CHECK(http.calls[0].path == "/api/factory_reset?confirm=yes");
}

TEST_CASE("factoryReset sends no token header before any config fetch") {
  FakeHttp http;
  http.fallback = ok("{}");
  neon::client::DeviceClient c(http);
  REQUIRE(c.factoryReset().ok);
  REQUIRE_FALSE(http.calls.empty());
  CHECK(http.calls[0].extra_header.empty());
}

TEST_CASE("factoryReset attaches the device token learned from getConfig") {
  FakeHttp http;
  http.by_path["/api/config"] =
      ok("{\"device_token\":\"0123456789abcdef0123456789abcdef\"}");
  http.fallback = ok("{}");
  neon::client::DeviceClient c(http);
  REQUIRE(c.getConfig().ok);
  REQUIRE(c.factoryReset().ok);
  REQUIRE(http.calls.size() == 2);
  CHECK(http.calls[1].path == "/api/factory_reset?confirm=yes");
  CHECK(http.calls[1].extra_header ==
        "X-Neon-Token: 0123456789abcdef0123456789abcdef");
}

TEST_CASE("config_put_body writes typed wifi and ap passwords") {
  neon::Config cfg{};
  std::strncpy(cfg.wifi[0].ssid, "studio", sizeof(cfg.wifi[0].ssid));
  std::strncpy(cfg.wifi[0].pass, "secret42", sizeof(cfg.wifi[0].pass));
  std::strncpy(cfg.ap_pass, "linkpass", sizeof(cfg.ap_pass));
  const std::string body = neon::client::config_put_body(cfg);
  CHECK(body.find("\"pass\":\"secret42\"") != std::string::npos);
  CHECK(body.find("\"pass\":\"linkpass\"") != std::string::npos);
}

TEST_CASE("getConfig fills secrets from has_pass") {
  FakeHttp http;
  http.by_path["/api/config"] = ok(
      "{\"wifi\":{\"networks\":[{\"ssid\":\"x\",\"pass\":\"\",\"has_pass\":true}]},"
      "\"ap\":{\"has_pass\":false}}");
  neon::client::DeviceClient c(http);
  neon::client::ConfigSecrets sec;
  auto r = c.getConfig(&sec);
  REQUIRE(r.ok);
  CHECK(sec.wifi_has_pass[0]);
  CHECK_FALSE(sec.ap_has_pass);
}

TEST_CASE("reboot treats a dropped connection as success") {
  FakeHttp http;
  neon::client::HttpResponse drop;
  drop.connect_failed = true;
  drop.error = "connection reset";
  http.by_path["/api/reboot"] = drop;
  neon::client::DeviceClient c(http);
  auto r = c.reboot();
  CHECK(r.ok);
}
