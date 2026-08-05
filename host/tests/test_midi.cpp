#include <doctest.h>

#include <cstdint>
#include <vector>

#include "neon/config/model.hpp"
#include "neon/midi/ble_midi_parser.hpp"
#include "neon/midi/midi_encoder.hpp"
#include "neon/midi/router.hpp"

namespace {

struct Recorder final : public neon::IMidiSink {
  std::vector<neon::MidiMessage> messages;
  std::vector<uint8_t> realtime;
  void on_message(const neon::MidiMessage& m) override {
    messages.push_back(m);
  }
  void on_realtime(uint8_t s) override { realtime.push_back(s); }
};

struct SinkRecorder final : public neon::IRouterSink {
  struct GateCall {
    uint8_t target;
    bool on;
  };
  std::vector<GateCall> gates;
  std::vector<uint16_t> cvs;
  std::vector<int32_t> latencies;
  std::vector<std::pair<uint8_t, uint8_t>> shuffles;
  std::vector<bool> transports;
  std::vector<uint8_t> trs;
  std::vector<uint8_t> programs;

  void gate(uint8_t target, bool on) override { gates.push_back({target, on}); }
  void program_change(uint8_t p) override { programs.push_back(p); }
  void pitch_cv(uint16_t r) override { cvs.push_back(r); }
  void latency_offset(int32_t us) override { latencies.push_back(us); }
  void shuffle(uint8_t idx, uint8_t pct) override {
    shuffles.push_back({idx, pct});
  }
  void transport(bool play) override { transports.push_back(play); }
  void trs_realtime(uint8_t s) override { trs.push_back(s); }
};

}  // namespace

TEST_CASE("parser: single note-on packet") {
  Recorder rec;
  neon::BleMidiParser p(&rec);
  // header, timestamp, 90 3C 64
  const uint8_t pkt[] = {0x80, 0x80, 0x90, 0x3c, 0x64};
  p.feed_packet(pkt, sizeof(pkt));
  REQUIRE(rec.messages.size() == 1);
  CHECK(rec.messages[0].status == 0x90);
  CHECK(rec.messages[0].data1 == 0x3c);
  CHECK(rec.messages[0].data2 == 0x64);
}

TEST_CASE("parser: running status within a packet") {
  Recorder rec;
  neon::BleMidiParser p(&rec);
  // note-on, then two more note-ons via running status (with and without
  // interleaved timestamp byte).
  const uint8_t pkt[] = {0x80, 0x80, 0x90, 0x3c, 0x64,
                         0x3e, 0x50,              // no timestamp
                         0x81, 0x40, 0x22};       // timestamp then data
  p.feed_packet(pkt, sizeof(pkt));
  REQUIRE(rec.messages.size() == 3);
  CHECK(rec.messages[1].status == 0x90);
  CHECK(rec.messages[1].data1 == 0x3e);
  CHECK(rec.messages[2].data1 == 0x40);
  CHECK(rec.messages[2].data2 == 0x22);
}

TEST_CASE("parser: realtime interleaves and multiple messages") {
  Recorder rec;
  neon::BleMidiParser p(&rec);
  const uint8_t pkt[] = {0x80, 0x80, 0xf8, 0x80, 0x90, 0x40, 0x40,
                         0x80, 0xfc, 0x80, 0x80, 0x40, 0x00};
  p.feed_packet(pkt, sizeof(pkt));
  REQUIRE(rec.realtime.size() == 2);
  CHECK(rec.realtime[0] == 0xf8);
  CHECK(rec.realtime[1] == 0xfc);
  REQUIRE(rec.messages.size() == 2);
  CHECK(rec.messages[0].status == 0x90);
  CHECK(rec.messages[1].status == 0x80);
}

TEST_CASE("parser: sysex spanning packets is skipped without desync") {
  Recorder rec;
  neon::BleMidiParser p(&rec);
  const uint8_t pkt1[] = {0x80, 0x80, 0xf0, 0x01, 0x02, 0x03};
  const uint8_t pkt2[] = {0x80, 0x04, 0x05, 0x80, 0xf7,
                          0x80, 0x90, 0x30, 0x30};
  p.feed_packet(pkt1, sizeof(pkt1));
  p.feed_packet(pkt2, sizeof(pkt2));
  REQUIRE(rec.messages.size() == 1);
  CHECK(rec.messages[0].status == 0x90);
  CHECK(rec.messages[0].data1 == 0x30);
}

TEST_CASE("parser: pathological input never produces garbage") {
  Recorder rec;
  neon::BleMidiParser p(&rec);
  const uint8_t junk1[] = {0x00};                    // bad header
  const uint8_t junk2[] = {0x80};                    // header only
  const uint8_t junk3[] = {0x80, 0x80, 0x90, 0x3c};  // truncated message
  const uint8_t junk4[] = {0x80, 0x12, 0x34, 0x56};  // stray data bytes
  p.feed_packet(junk1, sizeof(junk1));
  p.feed_packet(junk2, sizeof(junk2));
  p.feed_packet(junk3, sizeof(junk3));
  p.feed_packet(junk4, sizeof(junk4));
  CHECK(rec.messages.empty());
  CHECK(rec.realtime.empty());
  // Parser still healthy afterwards.
  const uint8_t ok[] = {0x80, 0x80, 0xb0, 0x10, 0x7f};
  p.feed_packet(ok, sizeof(ok));
  REQUIRE(rec.messages.size() == 1);
  CHECK(rec.messages[0].status == 0xb0);
}

TEST_CASE("encoder: byte-exact messages") {
  uint8_t buf[3];
  CHECK(neon::midi::note_on(0, 60, 100, buf) == 3);
  CHECK(buf[0] == 0x90);
  CHECK(buf[1] == 60);
  CHECK(buf[2] == 100);
  CHECK(neon::midi::note_off(9, 42, buf) == 3);
  CHECK(buf[0] == 0x89);
  CHECK(buf[1] == 42);
  CHECK(neon::midi::control_change(15, 74, 127, buf) == 3);
  CHECK(buf[0] == 0xbf);
  CHECK(buf[1] == 74);
  CHECK(buf[2] == 127);
}

TEST_CASE("pitch CV mapping: 1 V/oct over a 5-octave window") {
  CHECK(neon::pitch_to_cv_q16(36) == 0);   // base note
  CHECK(neon::pitch_to_cv_q16(24) == 0);   // below base clamps
  CHECK(neon::pitch_to_cv_q16(96) == 65535);
  CHECK(neon::pitch_to_cv_q16(120) == 65535);
  // One octave up = 1/5 of full scale.
  const uint16_t oct = neon::pitch_to_cv_q16(48);
  CHECK(oct == 65535 * 12 / 60);
}

TEST_CASE("router: notes drive gate and pitch CV with last-note priority") {
  neon::MidiRouteConfig cfg;
  cfg.gate_target = 2;  // CLK3
  cfg.pitch_cv = true;
  SinkRecorder sink;
  neon::MidiRouter r(cfg, &sink);

  r.on_message({0x90, 60, 100, 3});
  REQUIRE(sink.gates.size() == 1);
  CHECK(sink.gates[0].target == 2);
  CHECK(sink.gates[0].on);
  REQUIRE(sink.cvs.size() == 1);

  // Second note retriggers CV; off of the *first* note is ignored.
  r.on_message({0x90, 64, 100, 3});
  r.on_message({0x80, 60, 0, 3});
  CHECK(sink.gates.size() == 2);  // only the second note-on added a gate
  // Off of the sounding note closes the gate.
  r.on_message({0x80, 64, 0, 3});
  REQUIRE(sink.gates.size() == 3);
  CHECK_FALSE(sink.gates[2].on);

  // Velocity-0 note-on acts as note-off.
  r.on_message({0x90, 50, 100, 3});
  r.on_message({0x90, 50, 0, 3});
  REQUIRE(sink.gates.size() == 5);
  CHECK_FALSE(sink.gates[4].on);
}

TEST_CASE("router: channel filter and omni") {
  neon::MidiRouteConfig cfg;
  cfg.gate_target = 0;
  cfg.midi_channel = 3;
  SinkRecorder sink;
  neon::MidiRouter r(cfg, &sink);
  r.on_message({0x90, 60, 100, 3});  // channel 0: filtered
  CHECK(sink.gates.empty());
  r.on_message({0x93, 60, 100, 3});  // channel 3: routed
  CHECK(sink.gates.size() == 1);

  cfg.midi_channel = 255;
  r.set_config(cfg);
  r.on_message({0x95, 62, 100, 3});  // omni: any channel
  CHECK(sink.gates.size() == 2);
}

TEST_CASE("router: CC mappings for latency and shuffle") {
  neon::MidiRouteConfig cfg;
  cfg.cc_latency = 20;
  cfg.cc_shuffle_base = 16;
  SinkRecorder sink;
  neon::MidiRouter r(cfg, &sink);

  r.on_message({0xb0, 20, 64, 3});  // center -> 0 latency
  REQUIRE(sink.latencies.size() == 1);
  CHECK(sink.latencies[0] == 0);
  r.on_message({0xb0, 20, 127, 3});
  CHECK(sink.latencies[1] > 24000);
  r.on_message({0xb0, 20, 0, 3});
  CHECK(sink.latencies[2] == -25000);

  r.on_message({0xb0, 18, 127, 3});  // base+2 -> CLK3 shuffle, full = 75
  REQUIRE(sink.shuffles.size() == 1);
  CHECK(sink.shuffles[0].first == 2);
  CHECK(sink.shuffles[0].second == 75);

  r.on_message({0xb0, 42, 12, 3});  // unmapped CC: nothing
  CHECK(sink.latencies.size() == 3);
  CHECK(sink.shuffles.size() == 1);
}

TEST_CASE("router: all-notes-off closes the gate") {
  neon::MidiRouteConfig cfg;
  cfg.gate_target = 1;
  SinkRecorder sink;
  neon::MidiRouter r(cfg, &sink);
  r.on_message({0x90, 60, 100, 3});
  r.on_message({0xb0, 123, 0, 3});
  REQUIRE(sink.gates.size() == 2);
  CHECK_FALSE(sink.gates[1].on);
}

TEST_CASE("router: transport and clock policies") {
  neon::MidiRouteConfig cfg;
  SinkRecorder sink;
  neon::MidiRouter r(cfg, &sink);

  // Default: transport enabled, clock ignored.
  r.on_realtime(0xfa);
  r.on_realtime(0xf8);
  r.on_realtime(0xfc);
  REQUIRE(sink.transports.size() == 2);
  CHECK(sink.transports[0]);
  CHECK_FALSE(sink.transports[1]);
  CHECK(sink.trs.empty());

  // Replace: BLE clock and transport forwarded to TRS (session transport
  // still follows while enabled).
  cfg.clock_policy = neon::MidiRouteConfig::ClockPolicy::kReplace;
  r.set_config(cfg);
  r.on_realtime(0xf8);
  r.on_realtime(0xfa);
  REQUIRE(sink.trs.size() == 2);
  CHECK(sink.trs[0] == 0xf8);
  CHECK(sink.trs[1] == 0xfa);
  CHECK(sink.transports.size() == 3);

  // Transport disabled: start/stop no longer touch the session but still
  // forward to TRS under the replace policy.
  cfg.transport_enabled = false;
  r.set_config(cfg);
  r.on_realtime(0xfa);
  CHECK(sink.transports.size() == 3);
  CHECK(sink.trs.size() == 3);
}

TEST_CASE("router: program change recalls presets when enabled") {
  neon::MidiRouteConfig cfg;
  SinkRecorder sink;
  neon::MidiRouter r(cfg, &sink);
  r.on_message({0xc0, 2, 0, 2});
  REQUIRE(sink.programs.size() == 1);
  CHECK(sink.programs[0] == 2);

  cfg.pc_presets = false;
  r.set_config(cfg);
  r.on_message({0xc0, 3, 0, 2});
  CHECK(sink.programs.size() == 1);
}

TEST_CASE("config sanitize covers the BLE MIDI fields") {
  neon::Config cfg;
  cfg.midi.midi_channel = 99;
  cfg.midi.gate_target = 7;
  cfg.midi.cc_latency = 200;
  cfg.midi.cc_shuffle_base = 126;
  cfg.midi.clock_policy = static_cast<neon::MidiRouteConfig::ClockPolicy>(9);
  neon::config_sanitize(&cfg);
  CHECK(cfg.midi.midi_channel == 255);
  CHECK(cfg.midi.gate_target == neon::MidiRouteConfig::kTargetNone);
  CHECK(cfg.midi.cc_latency == neon::MidiRouteConfig::kCcOff);
  CHECK(cfg.midi.cc_shuffle_base == neon::MidiRouteConfig::kCcOff);
  CHECK(cfg.midi.clock_policy ==
        neon::MidiRouteConfig::ClockPolicy::kIgnore);
}
