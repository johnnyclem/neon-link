#include <doctest.h>

#include <cstring>
#include <string>
#include <vector>

#include "neon/osc/bindings.hpp"
#include "neon/osc/codec.hpp"

namespace osc = neon::osc;

namespace {

struct Captured {
  std::vector<std::pair<std::string, float>> calls;
  int reply = 1;
};

int capture_sink(void* ctx, const char* address, float value) {
  auto* c = static_cast<Captured*>(ctx);
  c->calls.emplace_back(address, value);
  return c->reply;
}

constexpr int64_t kMs = 1000;

}  // namespace

TEST_CASE("float message round-trips through encode and parse") {
  uint8_t buf[64];
  const size_t n = osc::encode_float("/neon/tempo", 128.5f, buf, sizeof(buf));
  REQUIRE(n > 0);
  CHECK(n % 4 == 0);
  Captured c;
  osc::DispatchStats st;
  CHECK(osc::parse_packet(buf, n, capture_sink, &c, &st));
  REQUIRE(c.calls.size() == 1);
  CHECK(c.calls[0].first == "/neon/tempo");
  CHECK(c.calls[0].second == doctest::Approx(128.5f));
  CHECK(st.messages == 1);
  CHECK(st.applied == 1);
}

TEST_CASE("int, T, and F arguments become floats") {
  uint8_t buf[64];
  Captured c;
  osc::DispatchStats st;
  size_t n = osc::encode_int("/neon/transport", 1, buf, sizeof(buf));
  REQUIRE(n > 0);
  CHECK(osc::parse_packet(buf, n, capture_sink, &c, &st));
  CHECK(c.calls.back().second == 1.0f);

  // Hand-built ,T message (encoders here only do f/i).
  uint8_t t[16] = {'/', 'g', 'o', '\0', ',', 'T', '\0', '\0'};
  CHECK(osc::parse_packet(t, 8, capture_sink, &c, &st));
  CHECK(c.calls.back().second == 1.0f);
  t[5] = 'F';
  CHECK(osc::parse_packet(t, 8, capture_sink, &c, &st));
  CHECK(c.calls.back().second == 0.0f);
}

TEST_CASE("sloppy padding and bad shapes are malformed, not dispatched") {
  Captured c;
  osc::DispatchStats st;
  // Non-NUL padding byte inside the address padding.
  uint8_t bad[16] = {'/', 'a', '\0', 'x', ',', 'f', '\0', '\0',
                     0,   0,   0,    0};
  CHECK_FALSE(osc::parse_packet(bad, 12, capture_sink, &c, &st));
  // Two-argument typetag.
  uint8_t two[16] = {'/', 'a', '\0', '\0', ',', 'f', 'f', '\0',
                     0,   0,   0,    0,    0,   0,   0,   0};
  CHECK_FALSE(osc::parse_packet(two, 16, capture_sink, &c, &st));
  // Unaligned length.
  uint8_t ok[16] = {'/', 'a', '\0', '\0', ',', 'T', '\0', '\0'};
  CHECK_FALSE(osc::parse_packet(ok, 7, capture_sink, &c, &st));
  CHECK(c.calls.empty());
}

TEST_CASE("non-finite floats are counted as rejected, not applied") {
  uint8_t buf[16] = {'/', 'a', '\0', '\0', ',', 'f', '\0', '\0',
                     0x7f, 0xc0, 0x00, 0x00};  // NaN
  Captured c;
  osc::DispatchStats st;
  CHECK(osc::parse_packet(buf, 12, capture_sink, &c, &st));
  CHECK(c.calls.empty());
  CHECK(st.rejected_values == 1);
}

TEST_CASE("unknown paths and rejected values are counts, not errors") {
  uint8_t buf[64];
  const size_t n = osc::encode_float("/nope", 1.0f, buf, sizeof(buf));
  Captured c;
  c.reply = 0;
  osc::DispatchStats st;
  CHECK(osc::parse_packet(buf, n, capture_sink, &c, &st));
  CHECK(st.unknown_paths == 1);
  c.reply = -1;
  CHECK(osc::parse_packet(buf, n, capture_sink, &c, &st));
  CHECK(st.rejected_values == 1);
}

TEST_CASE("bundles need the immediate timetag and honor the message cap") {
  uint8_t msg[64];
  const size_t mn = osc::encode_float("/neon/tempo", 120.0f, msg, sizeof(msg));
  REQUIRE(mn > 0);

  auto build_bundle = [&](uint32_t tt_hi, uint32_t tt_lo, int count,
                          std::vector<uint8_t>* out) {
    out->assign({'#', 'b', 'u', 'n', 'd', 'l', 'e', '\0'});
    for (const uint32_t v : {tt_hi, tt_lo}) {
      out->push_back(static_cast<uint8_t>(v >> 24));
      out->push_back(static_cast<uint8_t>(v >> 16));
      out->push_back(static_cast<uint8_t>(v >> 8));
      out->push_back(static_cast<uint8_t>(v));
    }
    for (int i = 0; i < count; ++i) {
      const uint32_t sz = static_cast<uint32_t>(mn);
      out->push_back(static_cast<uint8_t>(sz >> 24));
      out->push_back(static_cast<uint8_t>(sz >> 16));
      out->push_back(static_cast<uint8_t>(sz >> 8));
      out->push_back(static_cast<uint8_t>(sz));
      out->insert(out->end(), msg, msg + mn);
    }
  };

  Captured c;
  osc::DispatchStats st;
  std::vector<uint8_t> b;
  build_bundle(0, 1, 3, &b);
  CHECK(osc::parse_packet(b.data(), b.size(), capture_sink, &c, &st));
  CHECK(st.applied == 3);

  build_bundle(0, 2, 1, &b);  // scheduled timetag: refused
  CHECK_FALSE(osc::parse_packet(b.data(), b.size(), capture_sink, &c, &st));

  c.calls.clear();
  build_bundle(0, 1, 12, &b);  // over the cap: first 8 applied
  CHECK(osc::parse_packet(b.data(), b.size(), capture_sink, &c, &st));
  CHECK(st.messages == osc::kMaxMessages);
}

TEST_CASE("address validation matches the OSC pattern rules") {
  CHECK(osc::address_valid("/neon/tempo"));
  CHECK_FALSE(osc::address_valid("neon/tempo"));
  CHECK_FALSE(osc::address_valid("/neon//tempo"));
  CHECK_FALSE(osc::address_valid("/neon/"));
  CHECK_FALSE(osc::address_valid("/neon/t empo"));
  CHECK_FALSE(osc::address_valid("/neon/t{a}"));
  CHECK_FALSE(osc::address_valid("/"));
}

TEST_CASE("encode_bang is a padded address plus empty typetag") {
  uint8_t buf[64];
  const size_t n =
      osc::encode_bang("/live/scene/fire_selected", buf, sizeof(buf));
  CHECK(n == 32);  // 26-char path -> 28 pad, plus 4 for ",\0\0\0"
  CHECK(std::memcmp(buf, "/live/scene/fire_selected", 26) == 0);
  CHECK(buf[26] == 0);
  CHECK(buf[27] == 0);
  CHECK(buf[28] == ',');
  CHECK(buf[29] == 0);
  CHECK(buf[30] == 0);
  CHECK(buf[31] == 0);
}

TEST_CASE("encode_bang rejects a bad address") {
  uint8_t buf[16];
  CHECK(osc::encode_bang("no-slash", buf, sizeof(buf)) == 0);
}

TEST_CASE("scalar bindings gate on delta against the last SENT value") {
  osc::OutBinding b;
  b.configure(osc::OutBinding::Kind::kScalar, 100, 0.5f);
  CHECK(b.due(0));
  CHECK(b.prepare(0, 120.0f));  // first value always sends
  b.note_sent();
  CHECK_FALSE(b.due(50 * kMs));
  CHECK(b.due(100 * kMs));
  // Creeps by 0.2 per sample: each individual step is under the delta,
  // but against last-sent it accumulates and eventually fires.
  CHECK_FALSE(b.prepare(100 * kMs, 120.2f));
  CHECK_FALSE(b.prepare(200 * kMs, 120.4f));
  CHECK(b.prepare(300 * kMs, 120.6f));
  CHECK(b.pending() == doctest::Approx(120.6f));
}

TEST_CASE("a failed send never suppresses the retry") {
  osc::OutBinding b;
  b.configure(osc::OutBinding::Kind::kScalar, 100, 0.5f);
  CHECK(b.prepare(0, 120.0f));
  // sendto failed: no note_sent(). The same value must fire again.
  CHECK(b.prepare(100 * kMs, 120.0f));
  b.note_sent();
  CHECK_FALSE(b.prepare(200 * kMs, 120.0f));
}

TEST_CASE("event bindings edge-filter and never fire on first sight") {
  osc::OutBinding b;
  b.configure(osc::OutBinding::Kind::kEvent, 50, 0.0f,
              osc::OutBinding::Edge::kBoth);
  CHECK_FALSE(b.prepare(0, 1.0f));  // first observation
  CHECK_FALSE(b.prepare(50 * kMs, 1.0f));
  CHECK(b.prepare(100 * kMs, 0.0f));  // falling edge
  CHECK(b.pending() == 0.0f);

  osc::OutBinding rising;
  rising.configure(osc::OutBinding::Kind::kEvent, 50, 0.0f,
                   osc::OutBinding::Edge::kRising);
  CHECK_FALSE(rising.prepare(0, 1.0f));
  CHECK_FALSE(rising.prepare(50 * kMs, 0.0f));  // falling: filtered
  CHECK(rising.prepare(100 * kMs, 1.0f));       // rising: fires
}
