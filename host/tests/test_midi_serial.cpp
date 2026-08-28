#include <doctest.h>

#include <cstdint>
#include <vector>

#include "neon/midi/serial_midi_parser.hpp"

namespace {

struct Recorder final : neon::IMidiSink {
  std::vector<neon::MidiMessage> messages;
  std::vector<uint8_t> realtime;

  void on_message(const neon::MidiMessage& m) override {
    messages.push_back(m);
  }
  std::vector<int64_t> realtime_t;
  void on_realtime(uint8_t status, int64_t t_us) override {
    realtime.push_back(status);
    realtime_t.push_back(t_us);
  }
};

void feed(neon::SerialMidiParser& p, std::initializer_list<uint8_t> bytes) {
  for (uint8_t b : bytes) {
    p.feed(b, 0);
  }
}

}  // namespace

TEST_CASE("serial midi: plain messages parse with the right lengths") {
  Recorder rec;
  neon::SerialMidiParser p(&rec);

  feed(p, {0x90, 60, 100});   // note on
  feed(p, {0x80, 60, 0});     // note off
  feed(p, {0xc2, 5});         // program change, channel 3
  feed(p, {0xe0, 0x00, 0x40}) /* pitch bend centre */;

  REQUIRE(rec.messages.size() == 4);
  CHECK(rec.messages[0].status == 0x90);
  CHECK(rec.messages[0].data1 == 60);
  CHECK(rec.messages[0].data2 == 100);
  CHECK(rec.messages[0].len == 3);
  CHECK(rec.messages[1].status == 0x80);
  CHECK(rec.messages[2].status == 0xc2);
  CHECK(rec.messages[2].data1 == 5);
  CHECK(rec.messages[2].len == 2);
  CHECK(rec.messages[3].status == 0xe0);
}

TEST_CASE("serial midi: running status carries across messages") {
  Recorder rec;
  neon::SerialMidiParser p(&rec);

  // One status byte, three note events (the classic keyboard stream).
  feed(p, {0x90, 60, 100, 64, 100, 60, 0});

  REQUIRE(rec.messages.size() == 3);
  for (const auto& m : rec.messages) {
    CHECK(m.status == 0x90);
    CHECK(m.len == 3);
  }
  CHECK(rec.messages[1].data1 == 64);
  CHECK(rec.messages[2].data2 == 0);  // velocity-0 note off
}

TEST_CASE("serial midi: realtime interleaves without corrupting a message") {
  Recorder rec;
  neon::SerialMidiParser p(&rec);

  // Clock bytes land between status and data, and between the data bytes.
  feed(p, {0x90, 0xf8, 60, 0xf8, 100});
  feed(p, {0xfa});  // start
  feed(p, {0xfc});  // stop

  REQUIRE(rec.messages.size() == 1);
  CHECK(rec.messages[0].status == 0x90);
  CHECK(rec.messages[0].data1 == 60);
  CHECK(rec.messages[0].data2 == 100);
  REQUIRE(rec.realtime.size() == 4);
  CHECK(rec.realtime[0] == 0xf8);
  CHECK(rec.realtime[2] == 0xfa);
  CHECK(rec.realtime[3] == 0xfc);
}

TEST_CASE("serial midi: sysex is skipped, realtime inside it still arrives") {
  Recorder rec;
  neon::SerialMidiParser p(&rec);

  feed(p, {0xf0, 0x7e, 0x7f, 0xf8, 0x06, 0x01, 0xf7});  // identity request
  feed(p, {0x90, 60, 100});

  REQUIRE(rec.messages.size() == 1);  // only the note; sysex swallowed
  CHECK(rec.messages[0].status == 0x90);
  REQUIRE(rec.realtime.size() == 1);
  CHECK(rec.realtime[0] == 0xf8);
}

TEST_CASE("serial midi: stray data bytes and system common behave") {
  Recorder rec;
  neon::SerialMidiParser p(&rec);

  feed(p, {42, 17});  // stray data with no status: dropped
  CHECK(rec.messages.empty());

  // Song position pointer parses...
  feed(p, {0xf2, 0x10, 0x02});
  REQUIRE(rec.messages.size() == 1);
  CHECK(rec.messages[0].status == 0xf2);
  CHECK(rec.messages[0].len == 3);

  // ...but does NOT establish running status: following data is dropped.
  feed(p, {0x33, 0x44});
  CHECK(rec.messages.size() == 1);

  // Tune request completes on arrival.
  feed(p, {0xf6});
  REQUIRE(rec.messages.size() == 2);
  CHECK(rec.messages[1].status == 0xf6);
  CHECK(rec.messages[1].len == 1);

  // A new channel status recovers the stream.
  feed(p, {0xb0, 123, 0});  // all notes off
  REQUIRE(rec.messages.size() == 3);
  CHECK(rec.messages[2].status == 0xb0);
  CHECK(rec.messages[2].data1 == 123);
}

TEST_CASE("serial midi: a status byte aborts a half-assembled message") {
  Recorder rec;
  neon::SerialMidiParser p(&rec);

  feed(p, {0x90, 60});        // half a note on...
  feed(p, {0xb0, 7, 100});    // ...pre-empted by a CC
  REQUIRE(rec.messages.size() == 1);
  CHECK(rec.messages[0].status == 0xb0);
  CHECK(rec.messages[0].data1 == 7);
}

TEST_CASE("serial midi: realtime bytes carry their per-byte timestamps") {
  Recorder rec;
  neon::SerialMidiParser p(&rec);
  p.feed(0xf8, 100);
  p.feed(0x90, 200);  // realtime timing must survive mid-message bytes
  p.feed(0xf8, 300);
  p.feed(60, 400);
  p.feed(100, 500);
  REQUIRE(rec.realtime.size() == 2);
  CHECK(rec.realtime_t[0] == 100);
  CHECK(rec.realtime_t[1] == 300);
  REQUIRE(rec.messages.size() == 1);
}
