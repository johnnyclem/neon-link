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

// "No sender-side timestamp": transports without in-band timestamps
// (serial wire MIDI) and BLE packets whose bytes carried none. The real
// values occupy 13 bits (0..8191 ms), so 0xffff can never collide.
inline constexpr uint16_t kNoSenderMs = 0xffff;

class IMidiSink {
 public:
  virtual ~IMidiSink() = default;
  virtual void on_message(const MidiMessage& m) = 0;
  // 0xF8..0xFF with the byte's arrival time in the shared µs timebase —
  // realtime bytes are timing, and timing without a timestamp is noise
  // (docs/SPIKE_MIDI_PLL.md §2). Feeders that truly have no clock pass 0.
  // sender_ms13 is the BLE-MIDI in-packet 13-bit millisecond stamp for
  // this byte (sender-side time, mod 8192 ms) or kNoSenderMs when the
  // transport carries none — the sync path maps it onto the local
  // timebase (ble_time_mapper.hpp) to see through BLE burst delivery.
  virtual void on_realtime(uint8_t status, int64_t t_us,
                           uint16_t sender_ms13) = 0;
};

// BLE-MIDI (MIDI over Bluetooth LE 1.0) packet parser: header byte,
// per-message timestamp bytes, running status, multiple messages per
// packet, realtime interleaving, SysEx spanning packets (SysEx payload is
// skipped — out of scope for v1 routing). The 13-bit millisecond
// timestamps (header carries bits 12..7, each timestamp byte bits 6..0,
// low-byte rollover within a packet increments the high bits) are
// reconstructed and delivered alongside every realtime byte; messages
// still act immediately. Malformed input never crashes the parser; bad
// bytes are skipped until the next plausible status.
class BleMidiParser {
 public:
  explicit BleMidiParser(IMidiSink* sink) : sink_(sink) {}

  // Feed one BLE-MIDI packet (one GATT write). rx_us is the packet's
  // arrival time; every realtime byte in the packet is delivered with it
  // plus the packet's most recently decoded 13-bit sender stamp (the
  // spec puts a timestamp byte before every event, so that is the
  // byte's own stamp on a conforming sender).
  void feed_packet(const uint8_t* data, size_t len, int64_t rx_us);

 private:
  static int data_bytes_for(uint8_t status);
  void deliver(uint8_t status, uint8_t d1, uint8_t d2);

  IMidiSink* sink_;
  bool in_sysex_ = false;
};

}  // namespace neon
