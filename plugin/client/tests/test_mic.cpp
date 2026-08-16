#include <doctest.h>

#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "neon/client/api.hpp"
#include "neon/client/mic.hpp"
#include "neon/client/mic_api.hpp"

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

struct FakeHttp : neon::client::HttpTransport {
  struct Call {
    std::string method;
    std::string host;
    int port = 0;
    std::string path;
    std::string body;
  };
  std::vector<Call> calls;
  std::map<std::string, neon::client::HttpResponse> by_path;
  neon::client::HttpResponse fallback;

  neon::client::HttpResponse request(const char* method, const char* host,
                                     int port, const char* path,
                                     const char* body, int,
                                     const char* = nullptr) override {
    Call c;
    c.method = method ? method : "";
    c.host = host ? host : "";
    c.port = port;
    c.path = path ? path : "";
    c.body = body ? body : "";
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

TEST_CASE("fixtures are phone-mic documents the sanitiser accepts") {
  const std::string status = load("mic_status.json");
  const std::string config = load("mic_config.json");
  CHECK(neon::client::probe_document_kind(status.c_str(), status.size()) ==
        neon::client::DocumentKind::PhoneMic);
  CHECK(neon::client::probe_document_kind(config.c_str(), config.size()) ==
        neon::client::DocumentKind::PhoneMic);

  neon::client::MicStatus s;
  neon::client::MicConfig c;
  REQUIRE(neon::client::parse_mic_status(status.c_str(), status.size(), &s));
  REQUIRE(neon::client::parse_mic_config(config.c_str(), config.size(), &c));
  CHECK(s.kind == neon::client::kMicKind);
  CHECK(s.on_session);
  CHECK(s.streaming);
  CHECK(s.ip == "10.0.0.81");
  CHECK(s.subscribers == 1);
  CHECK(s.latency_ms == doctest::Approx(12.4));
  CHECK(neon::client::presence_word(s) == neon::client::PresenceWord::Live);
  CHECK(c.peer_name == "iPhone");
  CHECK(c.gain_db == doctest::Approx(0.0));
  CHECK(c.rev == 4);
}

TEST_CASE("a module status (no kind) is not a phone document") {
  const char* module = "{\"bpm\":120,\"peers\":2,\"playing\":true,\"firmware\":\"0.9.0-beta2\"}";
  CHECK(neon::client::probe_document_kind(module, std::strlen(module)) ==
        neon::client::DocumentKind::NeonLink);
  neon::client::MicStatus s;
  CHECK_FALSE(neon::client::parse_mic_status(module, std::strlen(module), &s));

  const char* phone = "{\"kind\":\"phone-mic\"}";
  CHECK(neon::client::probe_document_kind(phone, std::strlen(phone)) ==
        neon::client::DocumentKind::PhoneMic);
  const char* toaster = "{\"kind\":\"toaster\"}";
  CHECK(neon::client::probe_document_kind(toaster, std::strlen(toaster)) ==
        neon::client::DocumentKind::Unknown);
  CHECK(neon::client::probe_document_kind("nope", 4) ==
        neon::client::DocumentKind::Unknown);
}

TEST_CASE("sanitize clamps gain, name, and forces kind") {
  neon::client::MicConfig c;
  c.peer_name = "  this name is far too long for a link channel  ";
  c.gain_db = 99;
  c.jitter_ms = 1;
  c.metronome_sound = neon::client::MetronomeSound::Sine;
  c.rev = 0;
  neon::client::sanitize_mic_config(&c);
  CHECK(c.kind == neon::client::kMicKind);
  CHECK(c.peer_name.size() <= neon::client::kMicPeerNameMax);
  CHECK(c.gain_db == doctest::Approx(36.0));
  CHECK(c.jitter_ms == 10);

  const char* raw =
      "{\"kind\":\"neon-link\",\"peer_name\":\"  this name is far too long for a "
      "link channel  \",\"gain_db\":99,\"jitter_ms\":1,\"metronome_sound\":\"cowbell\","
      "\"rev\":-4}";
  const auto merged = neon::client::merge_mic_config(neon::client::default_mic_config(),
                                                     raw, std::strlen(raw));
  CHECK(merged.kind == neon::client::kMicKind);
  CHECK(merged.peer_name.size() <= neon::client::kMicPeerNameMax);
  CHECK(merged.gain_db == doctest::Approx(36.0));
  CHECK(merged.jitter_ms == 10);
  CHECK(merged.metronome_sound == neon::client::MetronomeSound::Sine);
  CHECK(merged.rev == 0);
}

TEST_CASE("offline or idle status cannot claim LIVE") {
  neon::client::MicStatus idle;
  idle.streaming = true;
  idle.on_session = true;
  neon::client::sanitize_mic_status(&idle);
  CHECK(neon::client::presence_word(idle) == neon::client::PresenceWord::Idle);

  const char* local =
      "{\"kind\":\"phone-mic\",\"presence\":\"online\",\"streaming\":true,"
      "\"on_session\":false}";
  neon::client::MicStatus loc;
  REQUIRE(neon::client::parse_mic_status(local, std::strlen(local), &loc));
  CHECK(neon::client::presence_word(loc) == neon::client::PresenceWord::Local);

  const char* live =
      "{\"kind\":\"phone-mic\",\"presence\":\"online\",\"streaming\":true,"
      "\"on_session\":true}";
  neon::client::MicStatus lv;
  REQUIRE(neon::client::parse_mic_status(live, std::strlen(live), &lv));
  CHECK(neon::client::presence_word(lv) == neon::client::PresenceWord::Live);

  const char* forced =
      "{\"kind\":\"phone-mic\",\"presence\":\"offline\",\"streaming\":true,"
      "\"on_session\":true}";
  neon::client::MicStatus off;
  REQUIRE(neon::client::parse_mic_status(forced, std::strlen(forced), &off));
  CHECK_FALSE(off.streaming);
  CHECK_FALSE(off.on_session);
  CHECK(neon::client::presence_word(off) == neon::client::PresenceWord::Idle);
}

TEST_CASE("config patch sends only what changed plus kind") {
  const auto from = neon::client::default_mic_config();
  auto to = from;
  to.gain_db = 6;
  to.peer_name = "Booth";
  const std::string patch = neon::client::mic_config_patch_json(from, to);
  CHECK(patch.find("\"kind\":\"phone-mic\"") != std::string::npos);
  CHECK(patch.find("\"gain_db\":6") != std::string::npos);
  CHECK(patch.find("\"peer_name\":\"Booth\"") != std::string::npos);
  CHECK(patch.find("keep_alive") == std::string::npos);
  CHECK(patch.find("\"rev\"") == std::string::npos);
  const auto merged = neon::client::merge_mic_config(from, patch.c_str(), patch.size());
  CHECK(merged.gain_db == doctest::Approx(6.0));
  CHECK(merged.peer_name == "Booth");
  CHECK(merged.rev == from.rev);
}

TEST_CASE("parse_mic_host defaults to neon-mic.local:17001") {
  auto a = neon::client::parse_mic_host("");
  CHECK(a.host == "neon-mic.local");
  CHECK(a.port == 17001);
  auto b = neon::client::parse_mic_host("10.0.0.81");
  CHECK(b.host == "10.0.0.81");
  CHECK(b.port == 17001);
  auto c = neon::client::parse_mic_host("10.0.0.81:8080");
  CHECK(c.host == "10.0.0.81");
  CHECK(c.port == 8080);
  auto d = neon::client::parse_mic_host("http://neon-mic.local:17001/");
  CHECK(d.host == "neon-mic.local");
  CHECK(d.port == 17001);
  auto e = neon::client::parse_mic_host("http://127.0.0.1:8080/plugin");
  CHECK(e.host == "127.0.0.1");
  CHECK(e.port == 8080);
}

TEST_CASE("default documents are idle and dry") {
  const auto s = neon::client::default_mic_status();
  const auto c = neon::client::default_mic_config();
  CHECK(s.presence == neon::client::MicPresence::Offline);
  CHECK_FALSE(s.on_session);
  CHECK(c.gain_db == doctest::Approx(0.0));
  CHECK(c.publish);
}

TEST_CASE("MicClient getStatus rejects a module body") {
  FakeHttp http;
  http.by_path["/api/status"] = ok("{\"bpm\":120,\"device_name\":\"n\",\"hostname\":\"n.local\"}");
  neon::client::MicClient c(http);
  auto r = c.getStatus();
  CHECK_FALSE(r.ok);
  CHECK(r.error == neon::client::kKindMismatchModule);
  CHECK_FALSE(c.getConfig().ok);  // not called — no config path set
  REQUIRE_FALSE(http.calls.empty());
  CHECK(http.calls[0].port == 17001);
}

TEST_CASE("MicClient getStatus + capture talk to the phone documents") {
  FakeHttp http;
  http.by_path["/api/status"] = ok(load("mic_status.json"));
  http.by_path["/api/config"] = ok(load("mic_config.json"));
  http.by_path["/api/capture"] = ok("{\"ok\":true,\"streaming\":true}");
  http.by_path["/api/sources"] =
      ok("{\"sources\":[{\"id\":\"built-in\",\"label\":\"iPhone Microphone\"}]}");
  neon::client::MicClient c(http);
  c.setTarget("10.0.0.81", 17001);
  auto st = c.getStatus();
  REQUIRE(st.ok);
  CHECK(st.value.on_session);
  auto cfg = c.getConfig();
  REQUIRE(cfg.ok);
  CHECK(cfg.value.peer_name == "iPhone");
  auto src = c.getSources();
  REQUIRE(src.ok);
  REQUIRE(src.value.size() == 1);
  CHECK(src.value[0].id == "built-in");
  auto cap = c.capture(neon::client::CaptureOp::Start);
  REQUIRE(cap.ok);
  CHECK(cap.value.streaming);
  CHECK(http.calls[0].host == "10.0.0.81");
  CHECK(http.calls.back().body.find("\"op\":\"start\"") != std::string::npos);
}

TEST_CASE("DeviceClient getStatus rejects a phone-mic body") {
  FakeHttp http;
  http.by_path["/api/status"] = ok(load("mic_status.json"));
  neon::client::DeviceClient c(http);
  auto r = c.getStatus();
  CHECK_FALSE(r.ok);
  CHECK(r.error == neon::client::kKindMismatchMic);
}
