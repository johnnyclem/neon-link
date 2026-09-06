#include <doctest.h>

#include <cstdint>
#include <vector>

#include "neon/audio/tempo_follower.hpp"
#include "neon/config/model.hpp"

namespace {

struct Pub {
  int64_t t_us;
  uint32_t mbpm;
};

int64_t period_us(uint32_t milli_bpm) {
  return static_cast<int64_t>(60000000000ull / static_cast<uint64_t>(milli_bpm));
}

std::vector<Pub> feed_iois(neon::AudioTempoFollower& f, int64_t t0, int64_t dt,
                           int n_onsets) {
  std::vector<Pub> pubs;
  int64_t t = t0;
  for (int i = 0; i < n_onsets; ++i) {
    f.on_onset(t, 1.f);
    uint32_t mbpm = 0;
    if (f.take_tempo_update(&mbpm)) {
      pubs.push_back({t, mbpm});
    }
    t += dt;
  }
  return pubs;
}

uint32_t last_or_zero(const std::vector<Pub>& p) {
  return p.empty() ? 0u : p.back().mbpm;
}

}  // namespace

TEST_CASE("AudioTempoFollower: steady 120 quarters lock at 120 / subdiv 1") {
  neon::AudioTempoFollower f;
  f.set_session_tempo(120000);
  const auto pubs = feed_iois(f, 0, 500000, 16);
  REQUIRE_FALSE(pubs.empty());
  CHECK(f.lock_state() == neon::AudioTempoFollower::Lock::kLocked);
  CHECK(f.subdivision() == 1);
  CHECK(f.tempo_milli_bpm() == 120000);
  CHECK(last_or_zero(pubs) == 120000);
}

TEST_CASE("AudioTempoFollower: 120 8ths publish 120 / subdiv 2, not 240") {
  neon::AudioTempoFollower f;
  f.set_session_tempo(120000);
  const auto pubs = feed_iois(f, 0, 250000, 24);
  REQUIRE_FALSE(pubs.empty());
  CHECK(f.subdivision() == 2);
  CHECK(f.tempo_milli_bpm() == 120000);
  CHECK(last_or_zero(pubs) != 240000);
}

TEST_CASE("AudioTempoFollower: 16ths at 100 BPM → subdiv 4, 100 BPM") {
  neon::AudioTempoFollower f;
  f.set_session_tempo(100000);
  const auto pubs = feed_iois(f, 0, 150000, 32);
  REQUIRE_FALSE(pubs.empty());
  CHECK(f.subdivision() == 4);
  CHECK(f.tempo_milli_bpm() >= 99000);
  CHECK(f.tempo_milli_bpm() <= 101000);
}

TEST_CASE("AudioTempoFollower: kick 1+3 only is 120 / subdiv 1, not 60") {
  neon::AudioTempoFollower f;
  f.set_session_tempo(120000);
  const auto pubs = feed_iois(f, 0, 1000000, 16);
  REQUIRE_FALSE(pubs.empty());
  CHECK(f.subdivision() == 1);
  CHECK(f.tempo_milli_bpm() == 120000);
  CHECK(last_or_zero(pubs) != 60000);
}

TEST_CASE("AudioTempoFollower: kick 1+3 at 108, session 120 → ~108") {
  neon::AudioTempoFollower f;
  f.set_session_tempo(120000);
  // 108 BPM halves: 60e6/108 = 555555 us * 2 = 1111111 us IOI.
  const auto pubs = feed_iois(f, 0, 1111111, 16);
  REQUIRE_FALSE(pubs.empty());
  const uint32_t mbpm = f.tempo_milli_bpm();
  CHECK(mbpm >= 106000);
  CHECK(mbpm <= 110000);
  CHECK(mbpm != 60000);
  CHECK(mbpm != 98000);
  CHECK(mbpm != 120000);
  CHECK(f.subdivision() == 1);
}

TEST_CASE("AudioTempoFollower: kick 1+3 at 123 walks toward 123") {
  neon::AudioTempoFollower f;
  f.set_session_tempo(120000);
  // 123 BPM halves: 60e6/123 ≈ 487805 us * 2 = 976000 us (approx).
  const auto pubs = feed_iois(f, 0, 975610, 40);
  REQUIRE_FALSE(pubs.empty());
  CHECK(pubs.back().mbpm >= 121000);
  CHECK(pubs.back().mbpm <= 125000);
  for (size_t i = 1; i < pubs.size(); ++i) {
    const int64_t d = static_cast<int64_t>(pubs[i].mbpm) -
                      static_cast<int64_t>(pubs[i - 1].mbpm);
    const int64_t ad = d < 0 ? -d : d;
    CHECK(ad <= 2000);
    CHECK(pubs[i].t_us - pubs[i - 1].t_us >=
          neon::AudioTempoFollower::kTempoGapUs);
  }
}

TEST_CASE("AudioTempoFollower: acquire 108 from stored 120 (IOI 555 ms)") {
  neon::AudioTempoFollower f;
  f.set_session_tempo(120000);
  CHECK(f.lock_state() == neon::AudioTempoFollower::Lock::kIdle);
  const auto pubs = feed_iois(f, 0, 555000, 16);
  REQUIRE_FALSE(pubs.empty());
  CHECK(pubs[0].mbpm >= 106000);
  CHECK(pubs[0].mbpm <= 110000);
  CHECK(f.lock_state() == neon::AudioTempoFollower::Lock::kLocked);
}

TEST_CASE("AudioTempoFollower: +3 BPM drift over 64 beats") {
  neon::AudioTempoFollower f;
  f.set_session_tempo(120000);
  std::vector<Pub> pubs;
  int64_t t = 0;
  const int n = 65;
  for (int i = 0; i < n; ++i) {
    f.on_onset(t, 1.f);
    uint32_t mbpm = 0;
    if (f.take_tempo_update(&mbpm)) {
      pubs.push_back({t, mbpm});
    }
    // Linear 120 → 123 over 64 intervals.
    const uint32_t mb = 120000u + static_cast<uint32_t>(3000 * (i + 1) / 64);
    t += period_us(mb);
  }
  REQUIRE_FALSE(pubs.empty());
  CHECK(pubs.back().mbpm >= 122000);
  CHECK(pubs.back().mbpm <= 124000);
  for (size_t i = 1; i < pubs.size(); ++i) {
    CHECK(pubs[i].t_us - pubs[i - 1].t_us >=
          neon::AudioTempoFollower::kTempoGapUs);
    const int64_t d = static_cast<int64_t>(pubs[i].mbpm) -
                      static_cast<int64_t>(pubs[i - 1].mbpm);
    const int64_t ad = d < 0 ? -d : d;
    CHECK(ad <= 2000);
  }
}

TEST_CASE("AudioTempoFollower: 1.5 s dropout stays locked; 2 s relocks") {
  neon::AudioTempoFollower f;
  f.set_session_tempo(120000);
  auto pubs = feed_iois(f, 0, 500000, 12);
  REQUIRE_FALSE(pubs.empty());
  const int64_t last = 11 * 500000;
  CHECK(f.active(last + 1500000));
  CHECK(f.lock_state() == neon::AudioTempoFollower::Lock::kLocked);

  CHECK_FALSE(f.active(last + 3000000));
  CHECK(f.lock_state() == neon::AudioTempoFollower::Lock::kIdle);
  CHECK(f.subdivision() == 0);

  pubs = feed_iois(f, last + 4000000, 500000, 12);
  REQUIRE_FALSE(pubs.empty());
  CHECK(f.lock_state() == neon::AudioTempoFollower::Lock::kLocked);
  CHECK(f.tempo_milli_bpm() == 120000);
}

TEST_CASE("AudioTempoFollower: octave 8ths and 16ths stay at 120") {
  {
    neon::AudioTempoFollower f;
    f.set_session_tempo(120000);
    const auto pubs = feed_iois(f, 0, 250000, 24);
    REQUIRE_FALSE(pubs.empty());
    CHECK(f.subdivision() == 2);
    CHECK(f.tempo_milli_bpm() == 120000);
    CHECK(last_or_zero(pubs) != 240000);
  }
  {
    neon::AudioTempoFollower f;
    f.set_session_tempo(120000);
    const auto pubs = feed_iois(f, 0, 125000, 32);
    REQUIRE_FALSE(pubs.empty());
    CHECK(f.subdivision() == 4);
    CHECK(f.tempo_milli_bpm() == 120000);
    CHECK(last_or_zero(pubs) != 480000);
  }
}

TEST_CASE("AudioTempoFollower: clamp_milli_bpm, no publish outside 20–999") {
  neon::AudioTempoFollower f;
  f.set_session_tempo(120000);
  // Unclassifiable / absurd IOIs must not leak a tempo outside the clamp.
  int64_t t = 0;
  const int64_t weird[] = {1000, 5000, 15000, 30000000, 8000, 4000000};
  for (int k = 0; k < 40; ++k) {
    f.on_onset(t, 1.f);
    uint32_t mbpm = 0;
    if (f.take_tempo_update(&mbpm)) {
      CHECK(mbpm >= neon::kMinMilliBpm);
      CHECK(mbpm <= neon::kMaxMilliBpm);
    }
    t += weird[k % 6];
  }
  if (f.tempo_milli_bpm() != 0) {
    CHECK(f.tempo_milli_bpm() >= neon::kMinMilliBpm);
    CHECK(f.tempo_milli_bpm() <= neon::kMaxMilliBpm);
  }
}

TEST_CASE("AudioTempoFollower: snare 2+4 is the same 2T plant as 1+3") {
  neon::AudioTempoFollower f;
  f.set_session_tempo(120000);
  const auto pubs = feed_iois(f, 500000, 1000000, 16);
  REQUIRE_FALSE(pubs.empty());
  CHECK(f.subdivision() == 1);
  CHECK(f.tempo_milli_bpm() == 120000);
}

TEST_CASE("AudioTempoFollower: first classified IOI sets class, not ppqn=2") {
  neon::AudioTempoFollower f;
  f.set_session_tempo(120000);
  f.on_onset(0, 1.f);
  CHECK(f.subdivision() == 0);
  f.on_onset(500000, 1.f);
  CHECK(f.subdivision() == 1);
  CHECK(f.lock_state() == neon::AudioTempoFollower::Lock::kAcquiring);
}

TEST_CASE("AudioTempoFollower: kick on 1 at 108, session 120 → ~108") {
  neon::AudioTempoFollower f;
  f.set_session_tempo(120000);
  // 108 BPM wholes: 4 * (60e6/108) ≈ 2222222 us. Midpoints from dt, not T.
  const int64_t dt = 2222222;
  const auto pubs = feed_iois(f, 0, dt, 12);
  REQUIRE_FALSE(pubs.empty());
  const uint32_t mbpm = f.tempo_milli_bpm();
  CHECK(mbpm >= 106000);
  CHECK(mbpm <= 110000);
  CHECK(mbpm != 60000);
  CHECK(mbpm != 98000);
  CHECK(mbpm != 120000);
  CHECK(f.subdivision() == 1);

  const int64_t last = 11 * dt;
  CHECK(f.active(last + 1500000));
  CHECK(f.lock_state() == neon::AudioTempoFollower::Lock::kLocked);
  CHECK_FALSE(f.active(last + 9000000));
  CHECK(f.lock_state() == neon::AudioTempoFollower::Lock::kIdle);
}
