#pragma once

#include <cstdint>

// CLK IN / RST IN edge capture: rising-edge pin interrupts timestamp
// into a small ring drained by the link service (the Teensy counterpart
// of halesp::clkin_capture). A 1 ms lockout per input swallows contact
// bounce and ringing on unbuffered gate sources.
namespace clkin {

enum class Kind : uint8_t { kClock = 0, kReset = 1 };

struct Event {
  Kind kind;
  int64_t t_us;  // t41_now_us domain — shared with Link and the engine
};

void init(int clk_pin, int rst_pin);
bool pop(Event* out);

}  // namespace clkin
