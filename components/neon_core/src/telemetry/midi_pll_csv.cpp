#include "neon/telemetry/midi_pll_csv.hpp"

#include <cstdio>

namespace neon {

const char* midi_pll_transport_str(MidiClockPll::Transport t) {
  switch (t) {
    case MidiClockPll::Transport::kUsb:
      return "usb";
    case MidiClockPll::Transport::kBle:
      return "ble";
    case MidiClockPll::Transport::kDin:
    default:
      return "din";
  }
}

MidiPllTelemetrySample midi_pll_telemetry_sample(const MidiClockPll& pll,
                                                 int64_t t_us,
                                                 bool following) {
  MidiPllTelemetrySample s;
  s.t_us = t_us;
  s.tick = pll.ticks();
  s.transport = midi_pll_transport_str(pll.transport());
  s.residual_us = pll.residual_us();
  s.tempo_milli_bpm = pll.tempo_milli_bpm();
  s.locked = pll.locked() ? 1 : 0;
  s.playing = pll.playing() ? 1 : 0;
  MidiClockPll::Model m;
  s.beat_valid = pll.model(&m) && m.beat_valid ? 1 : 0;
  s.following = following ? 1 : 0;
  return s;
}

size_t midi_pll_telemetry_csv_header(char* out, size_t cap) {
  if (out == nullptr || cap == 0) {
    return 0;
  }
  const int n = std::snprintf(
      out, cap,
      "t_us,tick,transport,residual_us,tempo_milli_bpm,"
      "locked,playing,beat_valid,following");
  return n > 0 && static_cast<size_t>(n) < cap ? static_cast<size_t>(n) : 0;
}

size_t midi_pll_telemetry_csv_line(const MidiPllTelemetrySample& s, char* out,
                                   size_t cap) {
  if (out == nullptr || cap == 0) {
    return 0;
  }
  const int n = std::snprintf(
      out, cap, "%lld,%lu,%s,%lld,%lu,%u,%u,%u,%u",
      static_cast<long long>(s.t_us), static_cast<unsigned long>(s.tick),
      s.transport != nullptr ? s.transport : "din",
      static_cast<long long>(s.residual_us),
      static_cast<unsigned long>(s.tempo_milli_bpm),
      static_cast<unsigned>(s.locked), static_cast<unsigned>(s.playing),
      static_cast<unsigned>(s.beat_valid), static_cast<unsigned>(s.following));
  return n > 0 && static_cast<size_t>(n) < cap ? static_cast<size_t>(n) : 0;
}

}  // namespace neon
