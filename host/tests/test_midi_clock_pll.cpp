#include <doctest.h>

#include <cmath>
#include <cstdint>

#include "neon/midi/clock_pll.hpp"

namespace {

using neon::MidiClockPll;

// Synthetic sender: one tick per call, at the current tempo. Tempo steps
// and ramps just mutate bpm between calls; the emitted times are what a
// real sender's grid would do.
struct TickGen {
  double bpm = 120.0;
  int64_t t = 0;

  int64_t next() {
    const int64_t out = t;
    t += llround(60000000.0 / (24.0 * bpm));
    return out;
  }
};

void feed(MidiClockPll& pll, TickGen& gen, int ticks) {
  for (int i = 0; i < ticks; ++i) {
    pll.on_tick(gen.next());
  }
}

}  // namespace

TEST_CASE("steady 120 BPM DIN seeds, locks inside two bars, and is accurate") {
  MidiClockPll pll;
  TickGen gen;
  feed(pll, gen, 8);
  CHECK_FALSE(pll.valid());  // the seed window is nine ticks
  feed(pll, gen, 1);
  CHECK(pll.valid());
  CHECK_FALSE(pll.locked());

  feed(pll, gen, 39);  // 48 total — a quarter of the two-bar budget
  CHECK(pll.locked());

  feed(pll, gen, 192);
  // The generator's integer period (20833 us) is 120.0019 BPM.
  CHECK(pll.tempo_milli_bpm() >= 119900);
  CHECK(pll.tempo_milli_bpm() <= 120100);
  CHECK(pll.residual_us() >= -50);
  CHECK(pll.residual_us() <= 50);

  // The anchor sits on the last tick; on a clean grid it must agree with
  // the sender to well under a millisecond.
  MidiClockPll::Model m;
  REQUIRE(pll.model(&m));
  const int64_t true_last = gen.t - 20833;
  CHECK(m.origin_us >= true_last - 100);
  CHECK(m.origin_us <= true_last + 100);
}

TEST_CASE("±1 ms DIN jitter: locked, tempo tight, grid wander bounded") {
  MidiClockPll pll;
  // Deterministic zero-mean jitter, worst case for a DIN byte stream.
  const int64_t jit[16] = {900, -600, 300,  -1000, 700, -200, 500,  -800,
                           1000, -400, 100, -700,  600, -300, 800,  -900};
  int64_t true_last = 0;
  for (int i = 0; i < 480; ++i) {
    true_last = static_cast<int64_t>(i) * 20833;
    pll.on_tick(true_last + jit[i % 16]);
  }
  CHECK(pll.locked());
  CHECK(pll.tempo_milli_bpm() >= 119820);
  CHECK(pll.tempo_milli_bpm() <= 120180);

  // The disciplined grid must sit on the sender's true grid, not on the
  // jitter: the anchor stays within the spike's ±0.5 ms budget (plus
  // margin for the deterministic table's local bias).
  MidiClockPll::Model m;
  REQUIRE(pll.model(&m));
  CHECK(m.origin_us >= true_last - 600);
  CHECK(m.origin_us <= true_last + 600);
}

TEST_CASE("tempo step 120 -> 128 relocks within four seconds") {
  MidiClockPll pll;
  TickGen gen;
  feed(pll, gen, 96);
  CHECK(pll.locked());

  gen.bpm = 128.0;
  feed(pll, gen, 205);  // four seconds of 128 BPM ticks
  CHECK(pll.locked());
  CHECK(pll.tempo_milli_bpm() >= 127700);
  CHECK(pll.tempo_milli_bpm() <= 128300);
}

TEST_CASE("a 1 BPM/s ramp is tracked by the loop, not by step reseeds") {
  MidiClockPll pll;
  TickGen gen;
  feed(pll, gen, 96);
  CHECK(pll.locked());

  // 120 -> 126 over six seconds. The integral term follows a ramp with a
  // constant few-ms lag; the error must never reach the step threshold
  // (half a tick), or the loop would stair-step through reseeds.
  while (gen.bpm < 126.0) {
    const double period_s = 60.0 / (24.0 * gen.bpm);
    pll.on_tick(gen.next());
    const int64_t half_tick = llround(60000000.0 / (24.0 * gen.bpm)) / 2;
    CHECK(pll.residual_us() < half_tick);
    CHECK(pll.residual_us() > -half_tick);
    gen.bpm += period_s;  // +1 BPM per elapsed second
  }
  feed(pll, gen, 96);
  CHECK(pll.tempo_milli_bpm() >= 125700);
  CHECK(pll.tempo_milli_bpm() <= 126300);
}

TEST_CASE("Start makes the next tick beat zero and fires one downbeat") {
  MidiClockPll pll;
  TickGen gen;
  feed(pll, gen, 48);  // free-running clock: tempo known, position not
  MidiClockPll::Model m;
  REQUIRE(pll.model(&m));
  CHECK_FALSE(m.playing);
  CHECK_FALSE(m.beat_valid);

  int64_t downbeat = 0;
  CHECK_FALSE(pll.take_downbeat(&downbeat));
  pll.on_start(gen.t);
  const int64_t t0_true = gen.t;
  feed(pll, gen, 1);

  REQUIRE(pll.model(&m));
  CHECK(m.playing);
  CHECK(m.beat_valid);
  CHECK(m.beat_at_origin_q32 == 0);
  REQUIRE(pll.take_downbeat(&downbeat));
  CHECK(downbeat >= t0_true - 200);
  CHECK(downbeat <= t0_true + 200);
  CHECK_FALSE(pll.take_downbeat(&downbeat));

  feed(pll, gen, 24);
  REQUIRE(pll.model(&m));
  CHECK(m.beat_at_origin_q32 == (1ll << 32));  // exactly beat 1
}

TEST_CASE("Stop freezes the position; Continue resumes at the next tick") {
  MidiClockPll pll;
  TickGen gen;
  feed(pll, gen, 48);
  pll.on_start(gen.t);
  feed(pll, gen, 48);  // song ticks 0..47

  pll.on_stop(gen.t);
  MidiClockPll::Model m;
  REQUIRE(pll.model(&m));
  CHECK_FALSE(m.playing);
  CHECK_FALSE(m.beat_valid);

  // The clock keeps running while stopped (many devices do); the tempo
  // stays tracked but the position must not advance.
  feed(pll, gen, 24);
  REQUIRE(pll.model(&m));
  CHECK_FALSE(m.beat_valid);
  CHECK(pll.tempo_milli_bpm() >= 119900);
  CHECK(pll.tempo_milli_bpm() <= 120100);

  pll.on_continue(gen.t);
  feed(pll, gen, 1);
  REQUIRE(pll.model(&m));
  CHECK(m.playing);
  CHECK(m.beat_valid);
  CHECK(m.beat_at_origin_q32 == (2ll << 32));  // song tick 48 = beat 2
}

TEST_CASE("SPP then Continue resumes at sixteenth * 6 ticks") {
  MidiClockPll pll;
  TickGen gen;
  feed(pll, gen, 48);
  pll.on_spp(16);  // 16 sixteenths = 96 ticks = beat 4
  pll.on_continue(gen.t);
  feed(pll, gen, 1);
  MidiClockPll::Model m;
  REQUIRE(pll.model(&m));
  CHECK(m.playing);
  CHECK(m.beat_valid);
  CHECK(m.beat_at_origin_q32 == (4ll << 32));
}

TEST_CASE("Start pending before any clock lands on the first tick") {
  MidiClockPll pll;
  TickGen gen;
  pll.on_start(gen.t);
  feed(pll, gen, 1);
  CHECK(pll.playing());
  MidiClockPll::Model m;
  CHECK_FALSE(pll.model(&m));  // no rate yet — model unusable

  feed(pll, gen, 8);  // window fills, rate seeds
  REQUIRE(pll.model(&m));
  CHECK(m.playing);
  CHECK(m.beat_valid);
  // Anchor sits on pll tick 8 = song tick 8.
  CHECK(m.beat_at_origin_q32 ==
        static_cast<int64_t>((8ull << 32) / 24u));
}

TEST_CASE("BLE burst delivery still converges near the true tempo") {
  MidiClockPll pll;
  pll.set_transport(MidiClockPll::Transport::kBle);
  // Two ticks per packet, both stamped with the packet's arrival: tick
  // 2k and 2k+1 arrive together at the true time of tick 2k+1.
  for (int i = 0; i < 480; ++i) {
    const int64_t pair_end = (static_cast<int64_t>(i) | 1) * 20833;
    pll.on_tick(pair_end);
  }
  CHECK(pll.valid());
  CHECK(pll.ticks() == 480);  // bursts never masquerade as a stall
  // Degraded mode by design: raw arrivals bound tempo to a few percent
  // and honest lock is withheld until BLE timestamps are decoded.
  CHECK(pll.tempo_milli_bpm() >= 116400);
  CHECK(pll.tempo_milli_bpm() <= 123600);
  CHECK_FALSE(pll.locked());
}

TEST_CASE("silence deactivates and clears; a new clock re-acquires") {
  MidiClockPll pll;
  TickGen gen;
  feed(pll, gen, 48);
  CHECK(pll.active(gen.t + 100000));
  CHECK_FALSE(pll.active(gen.t + 1000000));  // beyond max(500 ms, 8 ticks)
  CHECK_FALSE(pll.valid());

  TickGen gen2;
  gen2.bpm = 100.0;
  gen2.t = gen.t + 5000000;
  feed(pll, gen2, 16);
  CHECK(pll.valid());
  CHECK(pll.tempo_milli_bpm() >= 99900);
  CHECK(pll.tempo_milli_bpm() <= 100100);
}

TEST_CASE("a stall longer than any musical period restarts in-line") {
  MidiClockPll pll;
  TickGen gen;
  feed(pll, gen, 48);
  pll.on_start(gen.t);
  feed(pll, gen, 24);
  CHECK(pll.playing());

  // 600 ms of silence, then the clock returns at a new tempo: the old
  // grid (and the stale transport) must not survive.
  gen.t += 600000;
  gen.bpm = 90.0;
  feed(pll, gen, 1);
  CHECK_FALSE(pll.valid());
  CHECK_FALSE(pll.playing());
  feed(pll, gen, 16);
  CHECK(pll.valid());
  CHECK(pll.tempo_milli_bpm() >= 89900);
  CHECK(pll.tempo_milli_bpm() <= 90100);
}

TEST_CASE("SPP during play jumps the position at the next tick") {
  MidiClockPll pll;
  TickGen gen;
  feed(pll, gen, 48);
  pll.on_start(gen.t);
  feed(pll, gen, 12);  // song ticks 0..11
  pll.on_spp(8);       // jump to 48 ticks = beat 2
  feed(pll, gen, 1);
  MidiClockPll::Model m;
  REQUIRE(pll.model(&m));
  CHECK(m.playing);
  CHECK(m.beat_at_origin_q32 == (2ll << 32));
}
