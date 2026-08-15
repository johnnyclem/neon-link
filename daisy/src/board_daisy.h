#pragma once

#include <cstdint>

// The one place that knows which Daisy-family board object this build
// runs on (DaisySeed for the Seed and Pod, DaisyPatchSM for
// patch.init()). Everything hardware-shaped that differs per board —
// init, QSPI access, the audio start, Tempo CV, the onboard LED — goes
// through these functions so the services stay board-agnostic.

#if defined(NEON_BOARD_PATCH_INIT)
#include "daisy_patch_sm.h"
extern daisy::patch_sm::DaisyPatchSM g_patch;
#else
#include "daisy_seed.h"
extern daisy::DaisySeed g_seed;
#endif

// SDRAM, QSPI (memory-mapped), codec, system clocks, and the Tempo CV
// DAC. Call once, before any service init.
void board_init();

// The QSPI flash the config store persists into (8 MB on both SOMs).
daisy::QSPIHandle& board_qspi();

// Sets 48 kHz / the engine block size and starts the callback.
void board_audio_start(daisy::AudioHandle::AudioCallback cb,
                       size_t block_frames);

// Tempo CV from the portable tempo_cv_ratio_q16 mapping. Seed/Pod: the
// bare 12-bit DAC pin (op-amp scale to 0-5 V externally); patch.init():
// the module's own 0-5 V CV OUT 2 stage.
void board_tempo_cv_write(uint16_t ratio_q16);

// The SOM's onboard user LED.
void board_set_led(bool on);
