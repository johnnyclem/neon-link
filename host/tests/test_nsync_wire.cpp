#include <doctest.h>

#include <cstring>

#include "nsync/wire.hpp"

using namespace nsync;

namespace {

Header header_for(uint64_t node, uint64_t session) {
  Header h;
  h.node_id = node;
  h.session_id = session;
  return h;
}

}  // namespace

TEST_CASE("wire: announce roundtrip preserves every field") {
  AnnounceMsg m;
  m.state.tl.seq = 42;
  m.state.tl.writer = 0xAABBCCDDEEFF0011ull;
  m.state.tl.origin_session_us = 123456789ull;
  m.state.tl.beat_at_origin_q32 = -(5ll << 32) - 12345;
  m.state.tl.tempo_mpb_q32 = 500000ull << 32;
  m.state.tl.quantum_mb = 8000;
  m.state.tr.seq = 7;
  m.state.tr.writer = 3;
  m.state.tr.toggle_session_us = 999999;
  m.state.tr.playing = 1;
  m.ghost_offset_us = -987654321;
  m.sender_local_tx_us = 1122334455ull;
  m.ttl_ms = 3500;

  uint8_t buf[kMaxPacket];
  const size_t n =
      encode_announce(header_for(0x0102030405060708ull, 0x1111ull), m, buf,
                      sizeof(buf));
  REQUIRE(n > 0);
  CHECK(n <= kMaxPacket);

  Header h;
  REQUIRE(decode_header(buf, n, h));
  CHECK(h.type == MsgType::kAnnounce);
  CHECK(h.node_id == 0x0102030405060708ull);
  CHECK(h.session_id == 0x1111ull);

  AnnounceMsg out;
  REQUIRE(decode_announce(buf, n, out));
  CHECK(out.state.tl.seq == m.state.tl.seq);
  CHECK(out.state.tl.writer == m.state.tl.writer);
  CHECK(out.state.tl.origin_session_us == m.state.tl.origin_session_us);
  CHECK(out.state.tl.beat_at_origin_q32 == m.state.tl.beat_at_origin_q32);
  CHECK(out.state.tl.tempo_mpb_q32 == m.state.tl.tempo_mpb_q32);
  CHECK(out.state.tl.quantum_mb == m.state.tl.quantum_mb);
  CHECK(out.state.tr.seq == m.state.tr.seq);
  CHECK(out.state.tr.writer == m.state.tr.writer);
  CHECK(out.state.tr.toggle_session_us == m.state.tr.toggle_session_us);
  CHECK(out.state.tr.playing == m.state.tr.playing);
  CHECK(out.ghost_offset_us == m.ghost_offset_us);
  CHECK(out.sender_local_tx_us == m.sender_local_tx_us);
  CHECK(out.ttl_ms == m.ttl_ms);
}

TEST_CASE("wire: ping/pong/tsf/bye roundtrip") {
  uint8_t buf[kMaxPacket];

  PingMsg ping;
  ping.t1_local_us = 0xDEADBEEF12345678ull;
  size_t n = encode_ping(header_for(1, 2), ping, buf, sizeof(buf));
  REQUIRE(n > 0);
  PingMsg ping_out;
  REQUIRE(decode_ping(buf, n, ping_out));
  CHECK(ping_out.t1_local_us == ping.t1_local_us);

  PongMsg pong;
  pong.t1_echo_us = 1;
  pong.t2_remote_rx_us = 2;
  pong.t3_remote_tx_us = 3;
  n = encode_pong(header_for(1, 2), pong, buf, sizeof(buf));
  REQUIRE(n > 0);
  PongMsg pong_out;
  REQUIRE(decode_pong(buf, n, pong_out));
  CHECK(pong_out.t1_echo_us == 1);
  CHECK(pong_out.t2_remote_rx_us == 2);
  CHECK(pong_out.t3_remote_tx_us == 3);

  TsfHintMsg tsf;
  const uint8_t bssid[6] = {0xA, 0xB, 0xC, 0xD, 0xE, 0xF};
  std::memcpy(tsf.bssid, bssid, 6);
  tsf.tsf_us = 77;
  tsf.local_us = 88;
  n = encode_tsf_hint(header_for(1, 2), tsf, buf, sizeof(buf));
  REQUIRE(n > 0);
  TsfHintMsg tsf_out;
  REQUIRE(decode_tsf_hint(buf, n, tsf_out));
  CHECK(std::memcmp(tsf_out.bssid, bssid, 6) == 0);
  CHECK(tsf_out.tsf_us == 77);
  CHECK(tsf_out.local_us == 88);

  n = encode_bye(header_for(9, 9), buf, sizeof(buf));
  REQUIRE(n > 0);
  Header h;
  REQUIRE(decode_header(buf, n, h));
  CHECK(h.type == MsgType::kBye);
}

TEST_CASE("wire: rejects garbage, truncation, bad version, wrong type") {
  uint8_t buf[kMaxPacket];
  PingMsg ping;
  ping.t1_local_us = 5;
  const size_t n = encode_ping(header_for(1, 2), ping, buf, sizeof(buf));
  REQUIRE(n > 0);

  Header h;
  // Truncated at every possible length.
  for (size_t cut = 0; cut < n; ++cut) {
    PingMsg out;
    CHECK_FALSE(decode_ping(buf, cut, out));
  }
  // Bad magic.
  uint8_t bad[kMaxPacket];
  std::memcpy(bad, buf, n);
  bad[0] = 'X';
  CHECK_FALSE(decode_header(bad, n, h));
  // Future protocol version.
  std::memcpy(bad, buf, n);
  bad[4] = kProtoVersion + 1;
  CHECK_FALSE(decode_header(bad, n, h));
  // Right envelope, wrong type for the decoder.
  PongMsg pong_out;
  CHECK_FALSE(decode_pong(buf, n, pong_out));
  AnnounceMsg ann_out;
  CHECK_FALSE(decode_announce(buf, n, ann_out));
}

TEST_CASE("wire: encoders return 0 when the buffer is too small") {
  uint8_t buf[8];
  AnnounceMsg m;
  CHECK(encode_announce(header_for(1, 2), m, buf, sizeof(buf)) == 0);
  CHECK(encode_bye(header_for(1, 2), buf, 4) == 0);
}
