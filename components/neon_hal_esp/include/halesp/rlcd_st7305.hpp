#pragma once

#include <cstddef>
#include <cstdint>

namespace halesp {

// Waveshare ESP32-S3-RLCD-4.2: 4.2" reflective mono LCD, ST7305
// controller, 300×400 native (400×300 landscape UI). Init sequence,
// register table, and the mirrored CASET addressing follow the vendor
// firmware for this exact glass (SolarOS rlcd_st7305 / Waveshare demo).
//
// The controller scans continuously — no BUSY pin, no refresh flash,
// no ghosting. Frames are the neon::rlcd packed format (200 rows ×
// 75 bytes); presents diff against a shadow copy and only stream the
// dirty row spans, so a cursor move costs a few hundred SPI bytes.
// All calls must come from one task (the rlcd service owns the glass).

bool rlcd_st7305_init(int sck, int mosi, int cs, int dc, int rst);

// Pushes a packed frame (neon::rlcd::kPackedSize bytes). Only dirty
// controller-row spans are transmitted; the first present after init
// streams everything.
void rlcd_st7305_present(const uint8_t* packed);

// HPM (0x38, 32 Hz scan) for live interaction, LPM (0x39, 1 Hz scan)
// for the idle microamp hold. Single-byte mode hop, safe to call
// every loop — redundant switches are dropped.
void rlcd_st7305_set_power(bool hpm);

// Blanks the glass and puts the controller into sleep-in. Used on the
// way into deep sleep; the next boot re-runs init.
void rlcd_st7305_display_off();

}  // namespace halesp
