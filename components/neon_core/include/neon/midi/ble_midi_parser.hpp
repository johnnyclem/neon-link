#pragma once

#include <cstddef>
#include <cstdint>

namespace neon {

// A parsed channel-voice / system-common MIDI message (realtime bytes are
// delivered separately — they can interleave anywhere).
struct MidiMessage {
  uint8_t status = 0;  // includes channel nibble
  uint8_t data1 = 0;
  uint8_t data2 = 0;
  uint8_t len = 0;  // total bytes incl. status (1..3)
};

class IMidiSink {
 public:
  virtual ~IMidiSink() = default;
  virtual void on_message(const MidiMessage& m) = 0;
  virtual void on_realtime(uint8_t status) = 0;  // 0xF8..0xFF
};

// BLE-MIDI (MIDI over Bluetooth LE 1.0) packet parser: header byte,
// per-message timestamp bytes, running status, multiple messages per
// packet, realtime interleaving, SysEx spanning packets (SysEx payload is
// skipped — out of scope for v1 routing). Timestamps are parsed but not
// used (messages act immediately). Malformed input never crashes the
// parser; bad bytes are skipped until the next plausible status.
class BleMidiParser {
 public:
  explicit BleMidiParser(IMidiSink* sink) : sink_(sink) {}

  // Feed one BLE-MIDI packet (one GATT write).
  void feed_packet(const uint8_t* data, size_t len);

 private:
  static int data_bytes_for(uint8_t status);
  void deliver(uint8_t status, uint8_t d1, uint8_t d2);

  IMidiSink* sink_;
  uint8_t running_status_ = 0;
  bool in_sysex_ = false;
};

}  // namespace neon
