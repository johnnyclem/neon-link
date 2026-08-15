#pragma once

#include <cstdint>

// Monotonic 64-bit microsecond clock, shared by the pulse engine, the
// timeline, MIDI, ext-clock capture, and the audio SampleClock (the
// Daisy counterpart of esp_timer / t41_now_us). Safe to call from ISRs
// and the main loop.
//
// Built on libDaisy's TIM2 tick (System::GetTick, 32-bit at the timer
// clock — 200 MHz stock — so it wraps every ~21 s). The wrap extender is
// serviced by every caller; the 100 µs pulse-timer ISR guarantees it is
// visited far inside the wrap period even if the main loop stalls.

// Cache the tick frequency. Call once after DaisySeed::Init().
void daisy_time_init();

int64_t daisy_now_us();
