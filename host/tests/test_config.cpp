#include <doctest.h>

#include <cstring>
#include <vector>

#include "fake_storage.hpp"
#include "neon/config/model.hpp"
#include "neon/tempo_cv.hpp"

TEST_CASE("crc32 known vector") {
  // CRC-32 (IEEE) of "123456789" is 0xCBF43926.
  const char* s = "123456789";
  CHECK(neon::crc32(reinterpret_cast<const uint8_t*>(s), 9) == 0xcbf43926u);
}

TEST_CASE("config round-trips through storage") {
  neon::Config cfg;
  cfg.engine.clocks[0].ppqn = 24;
  cfg.engine.clocks[1].shuffle_pct = 33;
  cfg.engine.clocks[2].mode = neon::ClockOutputConfig::PulseMode::kSquare;
  cfg.engine.clocks[2].duty_pct = 10;
  cfg.engine.reset_mode = neon::ResetMode::kEveryBar;
  cfg.engine.latency_us = -1234;
  cfg.tempo_cv_min_bpm = 40;
  cfg.tempo_cv_max_bpm = 240;

  std::vector<uint8_t> buf(neon::config_blob_size());
  REQUIRE(neon::config_encode(cfg, buf.data(), buf.size()) == buf.size());

  fakes::FakeStorage storage;
  REQUIRE(storage.write_blob("cfg", buf.data(), buf.size()));

  std::vector<uint8_t> back(buf.size());
  size_t len = 0;
  REQUIRE(storage.read_blob("cfg", back.data(), back.size(), &len));

  neon::Config out;
  REQUIRE(neon::config_decode(back.data(), len, &out));
  CHECK(out.engine.clocks[0].ppqn == 24);
  CHECK(out.engine.clocks[1].shuffle_pct == 33);
  CHECK(out.engine.clocks[2].mode ==
        neon::ClockOutputConfig::PulseMode::kSquare);
  CHECK(out.engine.clocks[2].duty_pct == 10);
  CHECK(out.engine.reset_mode == neon::ResetMode::kEveryBar);
  CHECK(out.engine.latency_us == -1234);
  CHECK(out.tempo_cv_min_bpm == 40);
  CHECK(out.tempo_cv_max_bpm == 240);
}

TEST_CASE("config decode rejects corruption, bad magic, bad size") {
  neon::Config cfg;
  std::vector<uint8_t> buf(neon::config_blob_size());
  REQUIRE(neon::config_encode(cfg, buf.data(), buf.size()) == buf.size());

  neon::Config out;
  SUBCASE("bit flip in payload") {
    buf[20] ^= 0x40;
    CHECK_FALSE(neon::config_decode(buf.data(), buf.size(), &out));
  }
  SUBCASE("bad magic") {
    buf[0] ^= 0xff;
    CHECK_FALSE(neon::config_decode(buf.data(), buf.size(), &out));
  }
  SUBCASE("truncated") {
    CHECK_FALSE(neon::config_decode(buf.data(), buf.size() - 5, &out));
  }
  SUBCASE("intact still decodes") {
    CHECK(neon::config_decode(buf.data(), buf.size(), &out));
  }
}

TEST_CASE("config sanitize clamps out-of-range values") {
  neon::Config cfg;
  cfg.engine.clocks[0].ppqn = 100000;
  cfg.engine.clocks[0].mult = 0;
  cfg.engine.clocks[0].shuffle_pct = 99;
  cfg.engine.latency_us = 9999999;
  cfg.tempo_cv_min_bpm = 500;
  cfg.tempo_cv_max_bpm = 100;
  neon::config_sanitize(&cfg);
  CHECK(cfg.engine.clocks[0].ppqn == 192);
  CHECK(cfg.engine.clocks[0].mult == 1);
  CHECK(cfg.engine.clocks[0].shuffle_pct == 75);
  CHECK(cfg.engine.latency_us == 50000);
  CHECK(cfg.tempo_cv_max_bpm > cfg.tempo_cv_min_bpm);
}

TEST_CASE("tempo CV mapping is linear and clamped") {
  CHECK(neon::tempo_cv_ratio_q16(20000, 20, 300) == 0);
  CHECK(neon::tempo_cv_ratio_q16(10000, 20, 300) == 0);
  CHECK(neon::tempo_cv_ratio_q16(300000, 20, 300) == 65535);
  CHECK(neon::tempo_cv_ratio_q16(400000, 20, 300) == 65535);
  const uint16_t mid = neon::tempo_cv_ratio_q16(160000, 20, 300);
  CHECK(mid == 65535 / 2);
}
