#include "neon/midi/router.hpp"

namespace neon {

uint16_t pitch_to_cv_q16(uint8_t note, uint8_t base_note) {
  if (note <= base_note) {
    return 0;
  }
  const uint32_t semis = note - base_note;
  if (semis >= 60) {
    return 65535;
  }
  return static_cast<uint16_t>(semis * 65535u / 60u);
}

bool MidiRouter::channel_match(uint8_t status) const {
  if (cfg_.midi_channel > 15) {
    return true;  // omni
  }
  return (status & 0x0fu) == cfg_.midi_channel;
}

void MidiRouter::on_message(const MidiMessage& m) {
  const uint8_t kind = m.status & 0xf0u;
  if (kind == 0xf0u) {
    return;  // system common: nothing routed in v1
  }
  if (!channel_match(m.status)) {
    return;
  }

  switch (kind) {
    case 0x90:  // note on (velocity 0 == off)
      if (m.data2 > 0) {
        active_note_ = m.data1;
        if (cfg_.pitch_cv) {
          sink_->pitch_cv(pitch_to_cv_q16(m.data1));
        }
        if (cfg_.gate_target != MidiRouteConfig::kTargetNone) {
          sink_->gate(cfg_.gate_target, true);
        }
        break;
      }
      [[fallthrough]];
    case 0x80:  // note off
      if (m.data1 == active_note_) {
        active_note_ = 255;
        if (cfg_.gate_target != MidiRouteConfig::kTargetNone) {
          sink_->gate(cfg_.gate_target, false);
        }
      }
      break;
    case 0xb0: {  // control change
      const uint8_t cc = m.data1;
      if (cc == 123) {  // all notes off
        if (active_note_ != 255 &&
            cfg_.gate_target != MidiRouteConfig::kTargetNone) {
          sink_->gate(cfg_.gate_target, false);
        }
        active_note_ = 255;
        break;
      }
      if (cfg_.cc_latency != MidiRouteConfig::kCcOff &&
          cc == cfg_.cc_latency) {
        // 0..127 -> -25 ms .. +25 ms, 64 = 0.
        const int32_t us =
            (static_cast<int32_t>(m.data2) - 64) * 25000 / 64;
        sink_->latency_offset(us);
      }
      if (cfg_.cc_shuffle_base != MidiRouteConfig::kCcOff &&
          cc >= cfg_.cc_shuffle_base &&
          cc < cfg_.cc_shuffle_base + 4) {
        const uint8_t pct =
            static_cast<uint8_t>(static_cast<uint32_t>(m.data2) * 75 / 127);
        sink_->shuffle(static_cast<uint8_t>(cc - cfg_.cc_shuffle_base), pct);
      }
      break;
    }
    default:
      break;  // program change: preset recall arrives in milestone 9
  }
}

void MidiRouter::on_realtime(uint8_t status) {
  switch (status) {
    case 0xfa:  // start
    case 0xfb:  // continue
      if (cfg_.transport_enabled) {
        sink_->transport(true);
      }
      break;
    case 0xfc:  // stop
      if (cfg_.transport_enabled) {
        sink_->transport(false);
      }
      break;
    case 0xf8:  // clock
      if (cfg_.clock_policy != MidiRouteConfig::ClockPolicy::kIgnore) {
        sink_->trs_realtime(status);
      }
      return;
    default:
      break;
  }
  // Start/stop/continue are also forwarded to TRS when BLE clock is in
  // charge of the TRS transport stream.
  if (cfg_.clock_policy != MidiRouteConfig::ClockPolicy::kIgnore &&
      (status == 0xfa || status == 0xfb || status == 0xfc)) {
    sink_->trs_realtime(status);
  }
}

}  // namespace neon
