#pragma once

#include <cstdint>

#include "neon/midi/ble_midi_parser.hpp"  // IMidiSink / MidiMessage

namespace neon {

// Serial (5-pin / TRS wire) MIDI stream parser: one byte at a time from
// a UART, delivered to the same IMidiSink the BLE parser feeds, so the
// router cannot tell which transport a message arrived on.
//
// Implements the wire rules that differ from BLE-MIDI's packet framing:
// running status (a status byte stays in force for subsequent data
// bytes), realtime bytes (0xF8..0xFF) interleaving anywhere — including
// mid-message — without disturbing the message being assembled, system
// common cancelling running status, and SysEx payloads skipped without
// losing the interleaved realtime stream. Malformed input never
// crashes the parser; stray data bytes are dropped until the next
// plausible status.
class SerialMidiParser {
 public:
  explicit SerialMidiParser(IMidiSink* sink) : sink_(sink) {}

  // Feed one wire byte.
  void feed(uint8_t byte);

 private:
  static int data_bytes_for(uint8_t status);

  IMidiSink* sink_;
  uint8_t status_ = 0;  // running status; 0 = none
  uint8_t data_[2] = {0, 0};
  int have_ = 0;
  bool in_sysex_ = false;
};

}  // namespace neon
