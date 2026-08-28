#include <doctest.h>

#include <cstdint>

#include "neon/clock_arbitration.hpp"
#include "neon/config/model.hpp"
#include "neon/ext_clock.hpp"

namespace {

// Feed a steady clock; returns the time after the last pulse.
int64_t feed_steady(neon::ExtClockEstimator& est, int64_t t0,
                    int64_t period_us, int pulses) {
  int64_t t = t0;
  for (int i = 0; i < pulses; ++i) {
    est.on_pulse(t);
    t += period_us;
  }
  return t - period_us;
}

int count_updates(neon::ExtClockEstimator& est) {
  int n = 0;
  uint32_t mbpm = 0;
  while (est.take_tempo_update(&mbpm)) {
    ++n;
  }
  return n;
}

}  // namespace

TEST_CASE("clean 120 BPM at 4 PPQN locks quickly and accurately") {
  neon::ExtClockEstimator est;
  est.set_input_ppqn(4);
  // 4 PPQN at 120 BPM -> 125 ms period.
  feed_steady(est, 0, 125000, 6);
  uint32_t mbpm = 0;
  REQUIRE(est.take_tempo_update(&mbpm));
  CHECK(mbpm >= 119900);
  CHECK(mbpm <= 120100);
}

TEST_CASE("input PPQN scales the estimate") {
  neon::ExtClockEstimator est24;
  est24.set_input_ppqn(24);
  feed_steady(est24, 0, 20833, 8);  // ~120 BPM at 24 PPQN
  uint32_t mbpm = 0;
  REQUIRE(est24.take_tempo_update(&mbpm));
  CHECK(mbpm >= 119500);
  CHECK(mbpm <= 120500);

  neon::ExtClockEstimator est1;
  est1.set_input_ppqn(1);
  feed_steady(est1, 0, 500000, 6);  // 120 BPM at 1 PPQN
  REQUIRE(est1.take_tempo_update(&mbpm));
  CHECK(mbpm >= 119900);
  CHECK(mbpm <= 120100);
}

TEST_CASE("jitter stays inside the hysteresis band with no update spam") {
  neon::ExtClockEstimator est;
  est.set_input_ppqn(4);
  // Deterministic +-2 ms jitter around 125 ms.
  const int64_t jit[8] = {1500, -1800, 900, -400, 2000, -1300, 700, -2000};
  int64_t t = 0;
  for (int i = 0; i < 64; ++i) {
    est.on_pulse(t + jit[i % 8]);
    t += 125000;
  }
  // Exactly one update: the initial lock. Jitter must not republish.
  CHECK(count_updates(est) == 1);
  CHECK(est.tempo_milli_bpm() >= 118000);
  CHECK(est.tempo_milli_bpm() <= 122000);
}

TEST_CASE("missing pulses are rejected as outliers") {
  neon::ExtClockEstimator est;
  est.set_input_ppqn(4);
  int64_t t = feed_steady(est, 0, 125000, 8);
  (void)count_updates(est);
  // Drop two pulses (gap of 3 periods), then resume.
  t += 3 * 125000;
  for (int i = 0; i < 8; ++i) {
    t += 125000;
    est.on_pulse(t);
  }
  // No tempo change should have been published from the gap.
  CHECK(count_updates(est) == 0);
  CHECK(est.tempo_milli_bpm() >= 119000);
  CHECK(est.tempo_milli_bpm() <= 121000);
}

TEST_CASE("glitch edges (contact bounce) are ignored") {
  neon::ExtClockEstimator est;
  est.set_input_ppqn(4);
  int64_t t = 0;
  for (int i = 0; i < 16; ++i) {
    est.on_pulse(t);
    if (i % 3 == 0) {
      est.on_pulse(t + 500);  // 0.5 ms bounce after the real edge
    }
    t += 125000;
  }
  (void)count_updates(est);
  CHECK(est.tempo_milli_bpm() >= 119500);
  CHECK(est.tempo_milli_bpm() <= 120500);
}

TEST_CASE("tempo step 120 -> 140 is followed after settling") {
  neon::ExtClockEstimator est;
  est.set_input_ppqn(4);
  int64_t t = feed_steady(est, 0, 125000, 8);
  (void)count_updates(est);
  // Step to 140 BPM (period 107142.86 us).
  for (int i = 0; i < 40; ++i) {
    t += 107143;
    est.on_pulse(t);
  }
  CHECK(count_updates(est) >= 1);
  CHECK(est.tempo_milli_bpm() >= 138000);
  CHECK(est.tempo_milli_bpm() <= 142000);
}

TEST_CASE("halved clock rate relocks instead of wedging") {
  neon::ExtClockEstimator est;
  est.set_input_ppqn(4);
  int64_t t = feed_steady(est, 0, 125000, 8);
  (void)count_updates(est);
  // 60 BPM: every period now looks like a dropout; the estimator must
  // relock rather than reject forever.
  for (int i = 0; i < 30; ++i) {
    t += 250000;
    est.on_pulse(t);
  }
  (void)count_updates(est);
  CHECK(est.tempo_milli_bpm() >= 59000);
  CHECK(est.tempo_milli_bpm() <= 61000);
}

TEST_CASE("timeout deactivates and clears; reactivation relocks") {
  neon::ExtClockEstimator est;
  est.set_input_ppqn(4);
  int64_t t = feed_steady(est, 0, 125000, 8);
  CHECK(est.active(t + 100000));
  // Silence beyond max(4x period, 2 s).
  CHECK_FALSE(est.active(t + 3000000));
  CHECK(est.tempo_milli_bpm() == 0);
  (void)count_updates(est);

  // New clock at 100 BPM (150 ms period at 4 PPQN).
  const int64_t t2 = t + 5000000;
  feed_steady(est, t2, 150000, 8);
  uint32_t mbpm = 0;
  REQUIRE(est.take_tempo_update(&mbpm));
  CHECK(mbpm >= 99500);
  CHECK(mbpm <= 100500);
}

TEST_CASE("reset edge produces exactly one phase request") {
  neon::ExtClockEstimator est;
  int64_t t = 0;
  CHECK_FALSE(est.take_phase_request(&t));
  est.on_reset(123456);
  REQUIRE(est.take_phase_request(&t));
  CHECK(t == 123456);
  CHECK_FALSE(est.take_phase_request(&t));
}

TEST_CASE("config sanitize covers the new clock-source fields") {
  neon::Config cfg;
  cfg.clock_in_ppqn = 5000;
  cfg.clock_source = static_cast<neon::ClockSource>(99);
  neon::config_sanitize(&cfg);
  CHECK(cfg.clock_in_ppqn == 96);
  CHECK(cfg.clock_source == neon::ClockSource::kAuto);
}

TEST_CASE("clock_source arbitration: the one precedence table") {
  using neon::ClockSource;
  const auto arb = [](ClockSource s, bool clk_in) {
    return neon::arbitrate_clock_source(s, clk_in);
  };

  // kAuto: CLK IN wins while alive, MIDI only in its absence.
  CHECK(arb(ClockSource::kAuto, true).follow_clk_in);
  CHECK_FALSE(arb(ClockSource::kAuto, true).midi_allowed);
  CHECK_FALSE(arb(ClockSource::kAuto, false).follow_clk_in);
  CHECK(arb(ClockSource::kAuto, false).midi_allowed);

  // kLinkMaster ignores both external sources.
  CHECK_FALSE(arb(ClockSource::kLinkMaster, true).follow_clk_in);
  CHECK_FALSE(arb(ClockSource::kLinkMaster, true).midi_allowed);

  // Each master mode pins its own source — and only its own: a pinned
  // MIDI master follows MIDI even while the jack is pulsing.
  CHECK(arb(ClockSource::kExternalMaster, true).follow_clk_in);
  CHECK_FALSE(arb(ClockSource::kExternalMaster, false).midi_allowed);
  CHECK(arb(ClockSource::kMidiMaster, true).midi_allowed);
  CHECK_FALSE(arb(ClockSource::kMidiMaster, true).follow_clk_in);
}
