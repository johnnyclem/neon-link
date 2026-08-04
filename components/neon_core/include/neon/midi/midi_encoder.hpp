#pragma once

#include <cstddef>
#include <cstdint>

namespace neon {

// Serial MIDI byte builders for the TRS output (31.25 kbaud UART).
// Byte-exact and trivially testable; no running status is emitted (every
// message is self-contained, maximizing receiver compatibility).
namespace midi {

inline constexpr uint8_t kClock = 0xf8;
inline constexpr uint8_t kStart = 0xfa;
inline constexpr uint8_t kContinue = 0xfb;
inline constexpr uint8_t kStop = 0xfc;

// Each returns the number of bytes written (buf must hold >= 3).
size_t note_on(uint8_t channel, uint8_t note, uint8_t velocity, uint8_t* buf);
size_t note_off(uint8_t channel, uint8_t note, uint8_t* buf);
size_t control_change(uint8_t channel, uint8_t cc, uint8_t value,
                      uint8_t* buf);

}  // namespace midi
}  // namespace neon
