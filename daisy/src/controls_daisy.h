#pragma once

#include <cstdint>

// The headless control surface: each screenless board (Pod,
// patch.init()) implements this interface in its controls_*.cpp and
// main_headless.cpp drives it. All tempo/transport intent goes through
// control_queue_push, so the link service keeps its single-owner
// contract exactly as on the UI builds — the only difference is who
// produces the commands.
namespace controls {

// What the board's indicators show each frame.
struct Leds {
  bool playing;
  bool beat;       // beat-flash window of the current beat
  bool run_level;  // live RUN channel level
  bool ext_clock;  // locked to CLK IN
};

void init();

// Called at 10 kHz from the pulse-timer tick (encoder sampling, where
// the board has encoders).
void sample_isr();

// Poll inputs and push control commands. Called every main-loop pass.
void poll(int64_t now_us, uint32_t now_ms);

// Refresh the board's LEDs (called at the UI frame rate).
void leds(const Leds& state);

}  // namespace controls
