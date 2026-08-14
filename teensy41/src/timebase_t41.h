#pragma once

#include <cstdint>

// Monotonic 64-bit microsecond clock, shared by the pulse engine, the
// timeline, and the UI (the Teensy counterpart of esp_timer). Safe to
// call from ISRs and the main loop; the wrap extender is serviced every
// pulse-timer tick, far inside micros()'s ~71-minute wrap period.
int64_t t41_now_us();
