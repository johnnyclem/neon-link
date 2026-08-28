#include "neon/midi/serial_midi_parser.hpp"

namespace neon {

int SerialMidiParser::data_bytes_for(uint8_t status) {
  switch (status & 0xf0u) {
    case 0x80:  // note off
    case 0x90:  // note on
    case 0xa0:  // poly aftertouch
    case 0xb0:  // control change
    case 0xe0:  // pitch bend
      return 2;
    case 0xc0:  // program change
    case 0xd0:  // channel aftertouch
      return 1;
    default:
      break;
  }
  switch (status) {
    case 0xf1:  // MTC quarter frame
    case 0xf3:  // song select
      return 1;
    case 0xf2:  // song position pointer
      return 2;
    default:
      return 0;  // tune request and anything unrecognised
  }
}

void SerialMidiParser::feed(uint8_t byte, int64_t t_us) {
  // Realtime interleaves anywhere — even between a status byte and its
  // data — and must not disturb the message being assembled.
  if (byte >= 0xf8u) {
    sink_->on_realtime(byte, t_us, kNoSenderMs);  // no in-band stamps on a wire
    return;
  }

  if (byte & 0x80u) {  // a status byte
    if (byte == 0xf0u) {  // SysEx start: skip the payload
      in_sysex_ = true;
      status_ = 0;
      have_ = 0;
      return;
    }
    if (byte == 0xf7u) {  // SysEx end
      in_sysex_ = false;
      return;
    }
    in_sysex_ = false;  // any status terminates a dangling SysEx
    have_ = 0;
    if (data_bytes_for(byte) == 0) {
      // Complete on arrival (tune request). System common also cancels
      // running status per the spec.
      MidiMessage m{byte, 0, 0, 1};
      sink_->on_message(m);
      status_ = 0;
      return;
    }
    status_ = byte;
    return;
  }

  // A data byte.
  if (in_sysex_ || status_ == 0) {
    return;  // SysEx payload, or stray data with no status in force
  }
  data_[have_++] = byte;
  const int need = data_bytes_for(status_);
  if (have_ < need) {
    return;
  }
  MidiMessage m{status_, data_[0], need > 1 ? data_[1] : uint8_t{0},
                static_cast<uint8_t>(1 + need)};
  sink_->on_message(m);
  have_ = 0;  // running status stays in force for the next data bytes
  if (status_ >= 0xf0u) {
    status_ = 0;  // ...but system common does not establish one
  }
}

}  // namespace neon
