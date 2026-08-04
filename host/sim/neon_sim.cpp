// neon_sim: print the edge timeline the clock engine would emit.
//
//   neon_sim <milli_bpm> <ppqn> <trig_len_us> <duration_ms>
//   e.g. neon_sim 120000 4 5000 1000

#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "neon/clock_engine.hpp"

int main(int argc, char** argv) {
  if (argc != 5) {
    std::fprintf(stderr,
                 "usage: %s <milli_bpm> <ppqn> <trig_len_us> <duration_ms>\n",
                 argv[0]);
    return 2;
  }
  const uint32_t milli_bpm = static_cast<uint32_t>(std::strtoul(argv[1], nullptr, 10));
  const uint32_t ppqn = static_cast<uint32_t>(std::strtoul(argv[2], nullptr, 10));
  const uint32_t trig_len = static_cast<uint32_t>(std::strtoul(argv[3], nullptr, 10));
  const int64_t duration_us = std::strtoll(argv[4], nullptr, 10) * 1000;

  neon::ClockEngine eng;
  eng.set_tempo(neon::micros_per_beat_q32_from_milli_bpm(milli_bpm));
  neon::OutputSettings s;
  s.ppqn = ppqn;
  s.trig_len_us = trig_len;
  eng.set_output(s);
  eng.reset(0);

  std::printf("# t_us channel edge\n");
  neon::Edge edges[64];
  size_t n;
  do {
    n = eng.generate(0, duration_us, edges, 64);
    for (size_t i = 0; i < n; ++i) {
      std::printf("%lld %u %s\n", static_cast<long long>(edges[i].t_us),
                  edges[i].channel, edges[i].high ? "rise" : "fall");
    }
  } while (n == 64);
  return 0;
}
