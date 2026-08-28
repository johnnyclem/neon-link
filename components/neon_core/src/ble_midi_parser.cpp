#include "neon/midi/ble_midi_parser.hpp"

namespace neon {

int BleMidiParser::data_bytes_for(uint8_t status) {
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
    case 0xf2:  // song position
      return 2;
    default:
      return 0;  // 0xF6 tune request etc.
  }
}

void BleMidiParser::deliver(uint8_t status, uint8_t d1, uint8_t d2) {
  MidiMessage m;
  m.status = status;
  m.data1 = d1;
  m.data2 = d2;
  m.len = static_cast<uint8_t>(1 + data_bytes_for(status));
  sink_->on_message(m);
}

void BleMidiParser::feed_packet(const uint8_t* data, size_t len,
                                int64_t rx_us) {
  if (len < 2 || (data[0] & 0x80u) == 0) {
    return;  // missing/invalid packet header
  }
  size_t i = 1;
  // Running status does not persist across BLE packets per spec; but a
  // packet may continue SysEx from the previous one.
  uint8_t running = 0;

  while (i < len) {
    const uint8_t b = data[i];

    if (in_sysex_) {
      if (b == 0xf7u) {
        in_sysex_ = false;
        ++i;
        continue;
      }
      if ((b & 0x80u) != 0 && b >= 0xf8u) {
        sink_->on_realtime(b, rx_us);  // realtime may interleave inside SysEx
        ++i;
        continue;
      }
      if ((b & 0x80u) != 0) {
        // Timestamp byte inside SysEx (precedes F7 or realtime) — or a
        // malformed abort. Peek: if next is F7/realtime treat as
        // timestamp, else abort SysEx and reprocess.
        if (i + 1 < len &&
            (data[i + 1] == 0xf7u || data[i + 1] >= 0xf8u)) {
          ++i;
          continue;
        }
        in_sysex_ = false;
        continue;  // reprocess b outside SysEx
      }
      ++i;  // SysEx payload byte: skipped (v1)
      continue;
    }

    if ((b & 0x80u) != 0) {
      // Timestamp byte; the next byte should be a status or (running
      // status) a data byte.
      ++i;
      if (i >= len) {
        break;
      }
      const uint8_t s = data[i];
      if ((s & 0x80u) != 0) {
        if (s >= 0xf8u) {
          sink_->on_realtime(s, rx_us);
          ++i;
          continue;
        }
        if (s == 0xf0u) {
          in_sysex_ = true;
          ++i;
          continue;
        }
        // Channel/system-common status.
        const int need = data_bytes_for(s);
        if (static_cast<size_t>(need) > len - 1 - i) {
          break;  // truncated packet
        }
        const uint8_t d1 = need >= 1 ? data[i + 1] & 0x7fu : 0;
        const uint8_t d2 = need >= 2 ? data[i + 2] & 0x7fu : 0;
        deliver(s, d1, d2);
        running = (s & 0xf0u) != 0xf0u ? s : 0;
        i += 1 + static_cast<size_t>(need);
        continue;
      }
      // Data byte after timestamp: running status continuation.
      if (running == 0) {
        ++i;  // stray data: skip
        continue;
      }
      const int need = data_bytes_for(running);
      if (static_cast<size_t>(need) > len - i) {
        break;
      }
      const uint8_t d1 = data[i] & 0x7fu;
      const uint8_t d2 = need >= 2 ? data[i + 1] & 0x7fu : 0;
      deliver(running, d1, d2);
      i += static_cast<size_t>(need);
      continue;
    }

    // Data byte without preceding timestamp: running status continuation.
    if (running == 0) {
      ++i;
      continue;
    }
    const int need = data_bytes_for(running);
    if (static_cast<size_t>(need) > len - i) {
      break;
    }
    const uint8_t d1 = data[i] & 0x7fu;
    const uint8_t d2 = need >= 2 ? data[i + 1] & 0x7fu : 0;
    deliver(running, d1, d2);
    i += static_cast<size_t>(need);
  }
}

}  // namespace neon
