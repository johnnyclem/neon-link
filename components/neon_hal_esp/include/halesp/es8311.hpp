#pragma once

// Waveshare P4 onboard ES8311 (I2C 0x18) + NS4150B PA.
// I2S slave, 32-bit Philips slots, 256fs MCLK — same packing as i2s_audio.

namespace halesp {

// Probe 0x18, program 48 kHz / 32-bit I2S, unmute DAC, raise PA_Ctrl.
// Call after I2S is enabled so MCLK is already running.
bool es8311_start();

// Mute DAC and drop PA_Ctrl. Safe if start() never ran.
void es8311_stop();

}  // namespace halesp
