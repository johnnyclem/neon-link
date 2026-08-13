#include <doctest.h>

#include <cmath>
#include <cstdint>
#include <vector>

#include "neon/audio/beat_window.hpp"
#include "neon/audio/click.hpp"
#include "neon/fixed_math.hpp"

namespace {

constexpr uint32_t kRate = 44100;
constexpr uint32_t kBlock = 128;

neon::TimelineSnapshot snapshot(uint32_t milli_bpm, int64_t origin_us,
                                double beat_at_origin, bool playing = true,
                                uint32_t quantum = 4) {
  neon::TimelineSnapshot tl;
  tl.tempo_mpb_q32 = neon::micros_per_beat_q32_from_milli_bpm(milli_bpm);
  tl.origin_us = origin_us;
  tl.beat_at_origin_q32 = static_cast<int64_t>(beat_at_origin * 4294967296.0);
  tl.quantum_beats = quantum;
  tl.playing = playing ? 1 : 0;
  return tl;
}

// Start time of block `b`. Derived from the absolute frame index rather
// than by accumulating a truncated per-block duration, which would drift a
// frame every few hundred blocks and turn this into a test of rounding.
int64_t block_t(int b) {
  return static_cast<int64_t>(b) * kBlock * 1000000 / kRate;
}

// Renders `blocks` contiguous blocks and returns the absolute frame index
// of every click onset.
std::vector<uint64_t> onsets(neon::ClickSynth& click,
                             const neon::TimelineSnapshot& tl, int blocks,
                             int64_t start_us = 0) {
  std::vector<uint64_t> out;
  std::vector<float> buf(kBlock);
  for (int b = 0; b < blocks; ++b) {
    const int64_t t0 = start_us + block_t(b);
    const int64_t t1 = start_us + block_t(b + 1);
    const neon::BeatWindow w = neon::beat_window(tl, t0, t1, kBlock);
    for (auto& s : buf) s = 0.0f;
    click.render(w, buf.data(), kBlock);
    if (click.last_onsets() != 0) {
      out.push_back(static_cast<uint64_t>(b) * kBlock +
                    click.last_onset_frame());
    }
  }
  return out;
}

neon::ClickConfig enabled_config(neon::ClickSound sound = neon::ClickSound::kSine) {
  neon::ClickConfig c;
  c.enabled = true;
  c.sound = sound;
  c.accent = true;
  return c;
}

}  // namespace

TEST_CASE("ClickSynth: onsets land on the exact beat sample") {
  const uint32_t tempi[] = {60000, 120000, 137500, 174000};
  for (uint32_t mbpm : tempi) {
    neon::ClickSynth click;
    click.reset(kRate);
    click.set_config(enabled_config());
    // Origin offset by a fraction of a block, so the beat never lines up
    // with a block boundary by accident.
    const neon::TimelineSnapshot tl = snapshot(mbpm, 137, 0.0);
    const auto hits = onsets(click, tl, 4000);
    REQUIRE(hits.size() >= 2);
    const double beat_us = 60000000.0 / (mbpm / 1000.0);
    for (size_t i = 0; i < hits.size(); ++i) {
      // Expected frame for beat i: (beat time - t0) in frames.
      const double t_us = 137.0 + beat_us * static_cast<double>(i);
      const double expect = t_us * kRate / 1000000.0;
      const double got = static_cast<double>(hits[i]);
      CHECK(std::fabs(got - expect) <= 1.0);
    }
  }
}

TEST_CASE("ClickSynth: accent only on beat 1 of the bar") {
  neon::ClickSynth click;
  click.reset(kRate);
  click.set_config(enabled_config());
  const neon::TimelineSnapshot tl = snapshot(120000, 0, 0.0);

  std::vector<float> buf(kBlock);
  std::vector<bool> accents;
  for (int b = 0; b < 4000; ++b) {
    const neon::BeatWindow w =
        neon::beat_window(tl, block_t(b), block_t(b + 1), kBlock);
    for (auto& s : buf) s = 0.0f;
    click.render(w, buf.data(), kBlock);
    if (click.last_onsets() != 0) {
      accents.push_back(click.last_onset_accent());
    }
  }
  REQUIRE(accents.size() >= 5);
  for (size_t i = 0; i < accents.size(); ++i) {
    CHECK(accents[i] == (i % 4 == 0));
  }
}

TEST_CASE("ClickSynth: a 3/4 quantum accents every third beat") {
  neon::ClickSynth click;
  click.reset(kRate);
  click.set_config(enabled_config());
  const neon::TimelineSnapshot tl = snapshot(120000, 0, 0.0, true, 3);

  std::vector<float> buf(kBlock);
  std::vector<bool> accents;
  for (int b = 0; b < 3000; ++b) {
    const neon::BeatWindow w =
        neon::beat_window(tl, block_t(b), block_t(b + 1), kBlock);
    for (auto& s : buf) s = 0.0f;
    click.render(w, buf.data(), kBlock);
    if (click.last_onsets() != 0) {
      accents.push_back(click.last_onset_accent());
    }
  }
  REQUIRE(accents.size() >= 4);
  for (size_t i = 0; i < accents.size(); ++i) {
    CHECK(accents[i] == (i % 3 == 0));
  }
}

TEST_CASE("ClickSynth: silent while the transport is stopped") {
  neon::ClickSynth click;
  click.reset(kRate);
  click.set_config(enabled_config());
  const neon::TimelineSnapshot tl = snapshot(120000, 0, 0.0, /*playing=*/false);
  CHECK(onsets(click, tl, 2000).empty());

  // Unless the user asked for a free-running click.
  neon::ClickConfig free_run = enabled_config();
  free_run.follow_transport = false;
  click.set_config(free_run);
  CHECK(onsets(click, tl, 2000).size() >= 2);
}

TEST_CASE("ClickSynth: disabled renders nothing at all") {
  neon::ClickSynth click;
  click.reset(kRate);
  neon::ClickConfig off;
  off.enabled = false;
  click.set_config(off);
  const neon::TimelineSnapshot tl = snapshot(120000, 0, 0.0);
  std::vector<float> buf(kBlock, 0.0f);
  for (int b = 0; b < 1000; ++b) {
    const neon::BeatWindow w =
        neon::beat_window(tl, block_t(b), block_t(b + 1), kBlock);
    click.render(w, buf.data(), kBlock);
  }
  for (float s : buf) {
    CHECK(s == 0.0f);
  }
}

TEST_CASE("ClickSynth: every sound renders deterministically") {
  const neon::ClickSound sounds[] = {neon::ClickSound::kSine,
                                     neon::ClickSound::kNoise,
                                     neon::ClickSound::kWood};
  for (neon::ClickSound sound : sounds) {
    std::vector<float> first, second;
    for (int pass = 0; pass < 2; ++pass) {
      neon::ClickSynth click;
      click.reset(kRate);
      click.set_config(enabled_config(sound));
      const neon::TimelineSnapshot tl = snapshot(120000, 0, 0.0);
      std::vector<float> all;
      std::vector<float> buf(kBlock);
      for (int b = 0; b < 40; ++b) {
        for (auto& s : buf) s = 0.0f;
        const neon::BeatWindow w = neon::beat_window(tl, block_t(b), block_t(b + 1), kBlock);
        click.render(w, buf.data(), kBlock);
        all.insert(all.end(), buf.begin(), buf.end());
      }
      (pass == 0 ? first : second) = all;
    }
    REQUIRE(first.size() == second.size());
    bool nonzero = false;
    for (size_t i = 0; i < first.size(); ++i) {
      CHECK(first[i] == second[i]);
      if (first[i] != 0.0f) nonzero = true;
    }
    CHECK(nonzero);
    // Nothing may leave the voice above full scale.
    for (float s : first) {
      CHECK(std::fabs(s) <= 1.0f);
    }
  }
}

TEST_CASE("ClickSynth: fade_out reaches silence within a millisecond") {
  neon::ClickSynth click;
  click.reset(kRate);
  click.set_config(enabled_config());
  const neon::TimelineSnapshot tl = snapshot(120000, 0, 0.0);
  std::vector<float> buf(kBlock);

  // Render until a click is sounding.
  int b = 0;
  while (!click.voice_active() && b < 100) {
    for (auto& s : buf) s = 0.0f;
    const neon::BeatWindow w = neon::beat_window(tl, block_t(b), block_t(b + 1), kBlock);
    click.render(w, buf.data(), kBlock);
    ++b;
  }
  REQUIRE(click.voice_active());

  click.fade_out();
  // 1 ms is 45 frames; one 128-frame block is more than enough.
  neon::BeatWindow silent;  // invalid window: no new onsets
  silent.frames = kBlock;
  for (auto& s : buf) s = 0.0f;
  click.render(silent, buf.data(), kBlock);
  CHECK_FALSE(click.voice_active());
  const uint32_t fade_frames = kRate * neon::ClickSynth::kFadeMs / 1000;
  for (uint32_t i = fade_frames + 1; i < kBlock; ++i) {
    CHECK(buf[i] == 0.0f);
  }
}
