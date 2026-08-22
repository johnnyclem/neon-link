#include <doctest.h>

#include <cstdlib>

#include "fake_net.hpp"
#include "nsync/node.hpp"

using fakenet::ClockModel;
using fakenet::FakeNet;
using nsync::Node;
using nsync::NodeConfig;

namespace {

NodeConfig cfg_for(uint64_t id) {
  NodeConfig c;
  c.node_id = id;
  return c;
}

// Emitter that counts and remembers the last packet, for single-node tests.
class SinkEmitter : public nsync::Emitter {
 public:
  void send(nsync::Addr dest, const uint8_t* data, size_t len) override {
    ++sends;
    last_dest = dest;
    last.assign(data, data + len);
  }
  int sends = 0;
  nsync::Addr last_dest = 0;
  std::vector<uint8_t> last;
};

}  // namespace

TEST_CASE("node: solo node free-runs like the stub") {
  Node n(cfg_for(1));
  n.set_quantum(8.0, 0);  // before start: applies at start
  n.start(120.0, 1000);
  hal::LinkState s;
  REQUIRE(n.capture(s, 1000));
  CHECK(s.tempo_bpm == doctest::Approx(120.0));
  CHECK(s.beat_at_origin == doctest::Approx(0.0));
  CHECK(s.quantum == doctest::Approx(8.0));
  CHECK(s.playing);
  CHECK(s.num_peers == 0);
  // One beat later at 120 BPM = 500 ms.
  REQUIRE(n.capture(s, 501000));
  CHECK(s.beat_at_origin == doctest::Approx(1.0).epsilon(0.001));
}

TEST_CASE("node: set_tempo re-anchors continuously and dedupes") {
  Node n(cfg_for(1));
  n.start(120.0, 0);
  const uint64_t seq0 = n.state().tl.seq;
  // Same tempo again: no state churn (the service calls this freely).
  n.set_tempo(120.0, 10000);
  CHECK(n.state().tl.seq == seq0);
  // 2 beats in, switch to 60 BPM: the beat count must not jump.
  n.set_tempo(60.0, 1000000);
  CHECK(n.state().tl.seq == seq0 + 1);
  hal::LinkState s;
  REQUIRE(n.capture(s, 1000000));
  CHECK(s.beat_at_origin == doctest::Approx(2.0).epsilon(0.001));
  REQUIRE(n.capture(s, 2000000));
  CHECK(s.beat_at_origin == doctest::Approx(3.0).epsilon(0.001));
}

TEST_CASE("node: request_beat_at_time puts beat 0 there") {
  Node n(cfg_for(1));
  n.start(120.0, 0);
  n.request_beat_at_time(10000000, 9000000);
  hal::LinkState s;
  REQUIRE(n.capture(s, 10000000));
  CHECK(s.beat_at_origin == doctest::Approx(0.0));
}

TEST_CASE("node: start_stop_sync off keeps transport private") {
  Node n(cfg_for(1));
  n.start(120.0, 0);
  n.set_start_stop_sync(false, 1000);
  const uint64_t tr_seq = n.state().tr.seq;
  n.set_playing(false, 2000);
  CHECK(n.state().tr.seq == tr_seq);  // shared state untouched
  hal::LinkState s;
  REQUIRE(n.capture(s, 3000));
  CHECK_FALSE(s.playing);
  n.set_playing(true, 4000);
  REQUIRE(n.capture(s, 5000));
  CHECK(s.playing);
}

TEST_CASE("node: two peers discover each other and agree") {
  FakeNet net(1);
  Node a(cfg_for(10)), b(cfg_for(20));
  net.add_node(&a, ClockModel{0, 0});
  net.add_node(&b, ClockModel{5000000, 0});  // b's clock is 5 s ahead
  a.start(120.0, net.local_time(0));
  b.start(100.0, net.local_time(1));
  net.run_for(3000000);

  hal::LinkState sa, sb;
  REQUIRE(a.capture(sa, net.local_time(0)));
  REQUIRE(b.capture(sb, net.local_time(1)));
  CHECK(sa.num_peers == 1);
  CHECK(sb.num_peers == 1);
  CHECK(a.session_id() == b.session_id());
  // Equal seq, tie broken by writer id: the higher node id's grid wins.
  CHECK(sa.tempo_bpm == doctest::Approx(100.0));
  CHECK(sb.tempo_bpm == doctest::Approx(100.0));
  CHECK(std::llabs(net.phase_error_us(0, 1)) < 2000);
}

TEST_CASE("node: tempo edits propagate either direction") {
  FakeNet net(2);
  Node a(cfg_for(10)), b(cfg_for(20));
  net.add_node(&a, ClockModel{0, 0});
  net.add_node(&b, ClockModel{-3000000, 0});
  a.start(120.0, net.local_time(0));
  b.start(120.0, net.local_time(1));
  net.run_for(3000000);

  a.set_tempo(133.0, net.local_time(0));  // lower id writes
  net.run_for(1000000);
  CHECK(net.tempo_bpm(1) == doctest::Approx(133.0));

  b.set_tempo(90.0, net.local_time(1));
  net.run_for(1000000);
  CHECK(net.tempo_bpm(0) == doctest::Approx(90.0));
}

TEST_CASE("node: last writer wins under concurrent edits") {
  FakeNet net(3);
  Node a(cfg_for(10)), b(cfg_for(20));
  net.add_node(&a, ClockModel{0, 0});
  net.add_node(&b, ClockModel{0, 0});
  a.start(120.0, net.local_time(0));
  b.start(120.0, net.local_time(1));
  net.run_for(3000000);

  // Simultaneous conflicting writes: both bump to the same seq; the
  // higher writer id must win on both ends.
  a.set_tempo(140.0, net.local_time(0));
  b.set_tempo(80.0, net.local_time(1));
  net.run_for(2000000);
  CHECK(net.tempo_bpm(0) == doctest::Approx(80.0));
  CHECK(net.tempo_bpm(1) == doctest::Approx(80.0));

  // A later single write beats it regardless of writer id.
  a.set_tempo(101.0, net.local_time(0));
  net.run_for(2000000);
  CHECK(net.tempo_bpm(0) == doctest::Approx(101.0));
  CHECK(net.tempo_bpm(1) == doctest::Approx(101.0));
}

TEST_CASE("node: transport intent propagates and applies on time") {
  FakeNet net(4);
  Node a(cfg_for(10)), b(cfg_for(20));
  net.add_node(&a, ClockModel{0, 0});
  net.add_node(&b, ClockModel{0, 0});
  a.start(120.0, net.local_time(0));
  b.start(120.0, net.local_time(1));
  net.run_for(3000000);

  a.set_playing(false, net.local_time(0));
  net.run_for(1000000);
  hal::LinkState sb;
  REQUIRE(b.capture(sb, net.local_time(1)));
  CHECK_FALSE(sb.playing);

  b.set_playing(true, net.local_time(1));
  net.run_for(1000000);
  hal::LinkState sa;
  REQUIRE(a.capture(sa, net.local_time(0)));
  CHECK(sa.playing);
}

TEST_CASE("node: BYE drops a peer immediately, silence drops it by TTL") {
  FakeNet net(5);
  Node a(cfg_for(10)), b(cfg_for(20)), c(cfg_for(30));
  net.add_node(&a, ClockModel{0, 0});
  net.add_node(&b, ClockModel{0, 0});
  net.add_node(&c, ClockModel{0, 0});
  a.start(120.0, net.local_time(0));
  b.start(120.0, net.local_time(1));
  c.start(120.0, net.local_time(2));
  net.run_for(3000000);
  CHECK(a.num_peers(net.local_time(0)) == 2);

  // c says goodbye cleanly: capture the BYE and inject it at a and b.
  {
    SinkEmitter sink;
    c.stop(sink);
    REQUIRE(sink.sends == 1);
    SinkEmitter replies;
    a.handle_packet(sink.last.data(), sink.last.size(), 3,
                    net.local_time(0), replies);
    b.handle_packet(sink.last.data(), sink.last.size(), 3,
                    net.local_time(1), replies);
  }
  net.silence(2, true);
  CHECK(a.num_peers(net.local_time(0)) == 1);
  CHECK(b.num_peers(net.local_time(1)) == 1);

  // b goes dark without a BYE: a still counts it until the TTL runs out.
  net.silence(1, true);
  CHECK(a.num_peers(net.local_time(0)) == 1);
  net.run_for(5000000);
  CHECK(a.num_peers(net.local_time(0)) == 0);
}

TEST_CASE("node: quantum changes travel with the session") {
  FakeNet net(6);
  Node a(cfg_for(10)), b(cfg_for(20));
  net.add_node(&a, ClockModel{0, 0});
  net.add_node(&b, ClockModel{0, 0});
  a.start(120.0, net.local_time(0));
  b.start(120.0, net.local_time(1));
  net.run_for(3000000);

  a.set_quantum(8.0, net.local_time(0));
  net.run_for(1000000);
  hal::LinkState sb;
  REQUIRE(b.capture(sb, net.local_time(1)));
  CHECK(sb.quantum == doctest::Approx(8.0));
}
