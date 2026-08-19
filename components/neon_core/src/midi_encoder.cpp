#include "neon/midi/midi_encoder.hpp"

namespace neon {
namespace midi {

size_t note_on(uint8_t channel, uint8_t note, uint8_t velocity,
               uint8_t* buf) {
  buf[0] = static_cast<uint8_t>(0x90u | (channel & 0x0fu));
  buf[1] = note & 0x7fu;
  buf[2] = velocity & 0x7fu;
  return 3;
}

size_t note_off(uint8_t channel, uint8_t note, uint8_t* buf) {
  buf[0] = static_cast<uint8_t>(0x80u | (channel & 0x0fu));
  buf[1] = note & 0x7fu;
  buf[2] = 0x40;
  return 3;
}

size_t control_change(uint8_t channel, uint8_t cc, uint8_t value,
                      uint8_t* buf) {
  buf[0] = static_cast<uint8_t>(0xb0u | (channel & 0x0fu));
  buf[1] = cc & 0x7fu;
  buf[2] = value & 0x7fu;
  return 3;
}

size_t song_position(uint16_t sixteenths, uint8_t* buf) {
  const uint16_t pos = static_cast<uint16_t>(sixteenths & 0x3fffu);
  buf[0] = kSongPosition;
  buf[1] = static_cast<uint8_t>(pos & 0x7fu);
  buf[2] = static_cast<uint8_t>((pos >> 7) & 0x7fu);
  return 3;
}

}  // namespace midi
}  // namespace neon
