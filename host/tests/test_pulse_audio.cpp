#include <doctest.h>

#include <cstdint>
#include <vector>

#include "neon/audio/pulse_render.hpp"
#include "neon/audio/types.hpp"
#include "neon/fixed_math.hpp"
#include "neon/multi_engine.hpp"

namespace {

constexpr uint32_t kRate = 44100;
constexpr uint32_t kBlock = 128;

int64_t block_t(int b) {
  return static_cast<int64_t>(b) * kBlock * 1000000 / kRate;
}

neon::TimelineSnapshot snapshot(uint32_t milli_bpm, bool playing) {
  neon::TimelineSnapshot tl;
  tl.tempo_mpb_q32 = neon::micros_per_beat_q32_from_milli_bpm(milli_bpm);
  tl.origin_us = 0;
  tl.beat_at_origin_q32 = 0;
  tl.quantum_beats = 4;
  tl.playing = playing ? 1 : 0;
  return tl;
}

neon::EngineConfig audio_engine_config() {
  neon::EngineConfig cfg;
  cfg.clocks[0].ppqn = 4;  // 16ths on CLK1, which the audio clock role taps
  cfg.clocks[1].enabled = false;
  cfg.clocks[2].enabled = false;
  cfg.clocks[3].enabled = false;
  cfg.reset_mode = neon::ResetMode::kEveryBar;
  return cfg;
}

// Absolute frame index of every edge PulseRender placed on `channel`,
// paired with its polarity.
struct FrameEdge {
  uint64_t frame;
  bool high;
};

std::vector<FrameEdge> render_edges(neon::PulseRender& pr, uint8_t channel,
                                    int blocks) {
  std::vector<FrameEdge> out;
  std::vector<float> buf(kBlock);
  for (int b = 0; b < blocks; ++b) {
    pr.begin_block(block_t(b), block_t(b + 1), kBlock);
    for (auto& s : buf) s = 0.0f;
    pr.render_channel(channel, 1.0f, buf.data(), kBlock);
    for (uint32_t i = 0; i < pr.edge_count(channel); ++i) {
      out.push_back(FrameEdge{static_cast<uint64_t>(b) * kBlock +
                                  pr.edge_frame(channel, i),
                              pr.edge_high(channel, i)});
    }
  }
  return out;
}

// The same edges, straight out of a MultiClockEngine driven over the same
// windows — the GPIO path's own numbers.
std::vector<FrameEdge> engine_edges(neon::MultiClockEngine& eng,
                                    uint8_t channel, int blocks) {
  std::vector<FrameEdge> out;
  neon::Edge buf[64];
  for (int b = 0; b < blocks; ++b) {
    const int64_t t0 = block_t(b);
    const int64_t t1 = block_t(b + 1);
    const size_t n = eng.generate(t0, t1, buf, 64);
    for (size_t i = 0; i < n; ++i) {
      if (buf[i].channel != channel) {
        continue;
      }
      const double frames_in =
          static_cast<double>(buf[i].t_us - t0) * kRate / 1000000.0;
      out.push_back(FrameEdge{
          static_cast<uint64_t>(b) * kBlock + static_cast<uint64_t>(frames_in),
          buf[i].high});
    }
  }
  return out;
}

}  // namespace

TEST_CASE("PulseRender: audio edges match the GPIO engine within a sample") {
  const neon::TimelineSnapshot tl = snapshot(128000, true);
  const neon::EngineConfig cfg = audio_engine_config();

  neon::PulseRender pr;
  pr.reset(kRate);
  pr.set_config(cfg);
  pr.retime(tl, 0);

  neon::MultiClockEngine eng;
  eng.set_config(cfg);
  eng.retime(tl, 0);

  const auto audio = render_edges(pr, neon::kChClk1, 400);
  const auto gpio = engine_edges(eng, neon::kChClk1, 400);

  REQUIRE(audio.size() == gpio.size());
  REQUIRE(audio.size() > 8);
  for (size_t i = 0; i < audio.size(); ++i) {
    CHECK(audio[i].high == gpio[i].high);
    const int64_t d = static_cast<int64_t>(audio[i].frame) -
                      static_cast<int64_t>(gpio[i].frame);
    CHECK(d <= 1);
    CHECK(d >= -1);
  }
}

TEST_CASE("PulseRender: the rendered gate follows the edges it placed") {
  const neon::TimelineSnapshot tl = snapshot(120000, true);
  neon::PulseRender pr;
  pr.reset(kRate);
  pr.set_config(audio_engine_config());
  pr.retime(tl, 0);

  std::vector<float> buf(kBlock);
  bool saw_high = false;
  bool saw_low = false;
  for (int b = 0; b < 200; ++b) {
    pr.begin_block(block_t(b), block_t(b + 1), kBlock);
    for (auto& s : buf) s = 0.0f;
    pr.render_channel(neon::kChClk1, 0.9f, buf.data(), kBlock);
    for (uint32_t i = 0; i < kBlock; ++i) {
      CHECK((buf[i] == 0.0f || buf[i] == 0.9f));
      if (buf[i] != 0.0f) saw_high = true;
      if (buf[i] == 0.0f) saw_low = true;
    }
  }
  CHECK(saw_high);
  CHECK(saw_low);
}

TEST_CASE("PulseRender: a gate that spans a block keeps its level") {
  const neon::TimelineSnapshot tl = snapshot(120000, true);
  neon::EngineConfig cfg = audio_engine_config();
  cfg.clocks[0].mode = neon::ClockOutputConfig::PulseMode::kSquare;
  cfg.clocks[0].ppqn = 1;  // quarter notes: 250 ms high at 120 BPM
  cfg.clocks[0].duty_pct = 50;

  neon::PulseRender pr;
  pr.reset(kRate);
  pr.set_config(cfg);
  pr.retime(tl, 0);

  std::vector<float> buf(kBlock);
  int all_high_blocks = 0;
  for (int b = 0; b < 200; ++b) {
    pr.begin_block(block_t(b), block_t(b + 1), kBlock);
    for (auto& s : buf) s = 0.0f;
    pr.render_channel(neon::kChClk1, 1.0f, buf.data(), kBlock);
    bool all_high = true;
    for (uint32_t i = 0; i < kBlock; ++i) {
      if (buf[i] != 1.0f) all_high = false;
    }
    if (all_high) ++all_high_blocks;
  }
  // 125 ms of high per beat is ~43 blocks; several of them carry no edge
  // at all and must still come out high.
  CHECK(all_high_blocks > 20);
}

TEST_CASE("PulseRender: RUN follows the transport, RESET fires on the bar") {
  neon::PulseRender pr;
  pr.reset(kRate);
  pr.set_config(audio_engine_config());

  pr.retime(snapshot(120000, false), 0);
  pr.begin_block(block_t(0), block_t(1), kBlock);
  CHECK_FALSE(pr.level(neon::kChRun));

  pr.retime(snapshot(120000, true), block_t(1));
  const auto run = render_edges(pr, neon::kChRun, 8);
  REQUIRE(run.size() >= 1);
  CHECK(run[0].high);
  CHECK(pr.level(neon::kChRun));

  // A 4-beat bar at 120 BPM is 2 s; RESET pulses once per bar.
  const auto reset = render_edges(pr, neon::kChReset, 800);
  REQUIRE(reset.size() >= 2);
  CHECK(reset[0].high);
  CHECK_FALSE(reset[1].high);
}

TEST_CASE("PulseRender: roles map onto the engine's channels") {
  CHECK(neon::PulseRender::channel_for_role(
            static_cast<uint8_t>(neon::AudioRole::kClock)) == neon::kChClk1);
  CHECK(neon::PulseRender::channel_for_role(
            static_cast<uint8_t>(neon::AudioRole::kReset)) == neon::kChReset);
  CHECK(neon::PulseRender::channel_for_role(
            static_cast<uint8_t>(neon::AudioRole::kRun)) == neon::kChRun);
  CHECK(neon::PulseRender::channel_for_role(
            static_cast<uint8_t>(neon::AudioRole::kMix)) == neon::kChannelCount);
  CHECK(neon::PulseRender::channel_for_role(
            static_cast<uint8_t>(neon::AudioRole::kMetronome)) ==
        neon::kChannelCount);
}
