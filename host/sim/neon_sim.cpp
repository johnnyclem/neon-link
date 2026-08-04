// neon_sim: print the merged edge timeline the output engine would emit
// with the default configuration (CLK1-4 at 4/2/1/24 PPQN, EveryBar reset,
// transport playing from beat 0).
//
//   neon_sim <milli_bpm> <duration_ms>
//   e.g. neon_sim 120000 1000

#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "neon/fixed_math.hpp"
#include "neon/multi_engine.hpp"

int main(int argc, char** argv) {
  if (argc != 3) {
    std::fprintf(stderr, "usage: %s <milli_bpm> <duration_ms>\n", argv[0]);
    return 2;
  }
  const uint32_t milli_bpm =
      static_cast<uint32_t>(std::strtoul(argv[1], nullptr, 10));
  const int64_t duration_us = std::strtoll(argv[2], nullptr, 10) * 1000;

  neon::TimelineSnapshot tl;
  tl.tempo_mpb_q32 = neon::micros_per_beat_q32_from_milli_bpm(milli_bpm);
  tl.origin_us = 0;
  tl.beat_at_origin_q32 = 0;
  tl.quantum_beats = 4;
  tl.playing = 1;

  neon::MultiClockEngine eng;
  neon::EngineConfig cfg;
  cfg.reset_mode = neon::ResetMode::kEveryBar;
  eng.set_config(cfg);
  eng.retime(tl, 0);

  static const char* kNames[neon::kChannelCount] = {"CLK1", "CLK2", "CLK3",
                                                    "CLK4", "RST", "RUN"};
  std::printf("# t_us channel edge\n");
  neon::Edge edges[64];
  size_t n;
  do {
    n = eng.generate(0, duration_us, edges, 64);
    for (size_t i = 0; i < n; ++i) {
      std::printf("%lld %s %s\n", static_cast<long long>(edges[i].t_us),
                  kNames[edges[i].channel], edges[i].high ? "rise" : "fall");
    }
  } while (n == 64);
  return 0;
}
