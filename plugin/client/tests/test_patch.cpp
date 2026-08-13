#include <doctest.h>

#include <cstring>
#include <string>

#include "neon/client/patch.hpp"
#include "neon/config/json.hpp"

TEST_CASE("clocks-index PATCH uses leading empty objects") {
  neon::client::JsonPatch p;
  p.setClockShuffle(2, 20);
  const std::string json = p.toJson();
  CHECK(json.find("\"clocks\":[{},{},{\"shuffle_pct\":20}]") !=
        std::string::npos);

  neon::Config cfg{};
  cfg.engine.clocks[0].shuffle_pct = 1;
  cfg.engine.clocks[1].shuffle_pct = 2;
  cfg.engine.clocks[2].shuffle_pct = 3;
  cfg.engine.clocks[3].shuffle_pct = 4;
  REQUIRE(neon::config_from_json(json.c_str(), json.size(), &cfg));
  CHECK(cfg.engine.clocks[0].shuffle_pct == 1);
  CHECK(cfg.engine.clocks[1].shuffle_pct == 2);
  CHECK(cfg.engine.clocks[2].shuffle_pct == 20);
  CHECK(cfg.engine.clocks[3].shuffle_pct == 4);
}

TEST_CASE("latency-only patch is a one-field document") {
  neon::client::JsonPatch p;
  p.setEngineLatency(-500);
  CHECK(p.toJson() == "{\"engine\":{\"latency_us\":-500}}");

  neon::Config cfg{};
  cfg.engine.latency_us = 0;
  cfg.quantum_beats = 8;
  REQUIRE(neon::config_from_json(p.toJson().c_str(), p.toJson().size(), &cfg));
  CHECK(cfg.engine.latency_us == -500);
  CHECK(cfg.quantum_beats == 8);
}

TEST_CASE("empty patch is {}") {
  neon::client::JsonPatch p;
  CHECK(p.empty());
  CHECK(p.toJson() == "{}");
}

TEST_CASE("ble + quantum + nudge") {
  neon::client::JsonPatch p;
  p.setBleEnabled(false);
  p.setQuantum(8);
  p.setMidiNudge(-1000);
  const std::string json = p.toJson();
  neon::Config cfg{};
  cfg.ble_enabled = 1;
  cfg.quantum_beats = 4;
  cfg.midi_nudge_us = 0;
  REQUIRE(neon::config_from_json(json.c_str(), json.size(), &cfg));
  CHECK(cfg.ble_enabled == 0);
  CHECK(cfg.quantum_beats == 8);
  CHECK(cfg.midi_nudge_us == -1000);
}
