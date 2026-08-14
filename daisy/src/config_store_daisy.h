#pragma once

#include <cstdint>

// Daisy-specific extension of the shared config_store.h API: the QSPI
// sector erase behind a persist is the worst main-loop stall on this
// target (tens to hundreds of ms), so the store lets the app register a
// hook that runs right before any blocking flash write. main.cpp uses it
// to top up the pulse-engine schedule far enough that the timer ISR
// never starves mid-erase (docs/DAISY.md §6, HANDOFF §6.2).
void neon_daisy_set_pre_persist_hook(void (*fn)(int64_t extra_horizon_us));
