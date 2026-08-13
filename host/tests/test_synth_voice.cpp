#include <doctest.h>

#include <cmath>
#include <cstdint>
#include <vector>

#include "neon/audio/synth_voice.hpp"

namespace {

constexpr uint32_t kRate = 44100;

float render_peak(neon::SynthVoiceBank& bank, uint32_t frames) {
  std::vector<float> buf(frames, 0.0f);
  bank.render(buf.data(), frames);
  float peak = 0.0f;
  for (float s : buf) {
    peak = std::fabs(s) > peak ? std::fabs(s) : peak;
  }
  return peak;
}

}  // namespace

TEST_CASE("SynthVoiceBank: a note sounds and a release ends it") {
  neon::SynthVoiceBank bank;
  bank.reset(kRate);
  CHECK(bank.active_voices() == 0);
  CHECK(render_peak(bank, 128) == 0.0f);

  bank.note_on(60, 100);
  CHECK(bank.active_voices() == 1);
  CHECK(render_peak(bank, 4410) > 0.01f);

  bank.note_off(60);
  // The release is a few hundred ms; give it a couple of seconds.
  for (int i = 0; i < 40; ++i) {
    render_peak(bank, 4410);
  }
  CHECK(bank.active_voices() == 0);
  CHECK(render_peak(bank, 128) == 0.0f);
}

TEST_CASE("SynthVoiceBank: velocity zero is a note off") {
  neon::SynthVoiceBank bank;
  bank.reset(kRate);
  bank.note_on(64, 90);
  CHECK(bank.active_voices() == 1);
  bank.note_on(64, 0);
  for (int i = 0; i < 40; ++i) {
    render_peak(bank, 4410);
  }
  CHECK(bank.active_voices() == 0);
}

TEST_CASE("SynthVoiceBank: polyphony is bounded and steals rather than fails") {
  neon::SynthVoiceBank bank;
  bank.reset(kRate);
  for (int n = 48; n < 48 + 20; ++n) {
    bank.note_on(static_cast<uint8_t>(n), 100);
  }
  CHECK(bank.active_voices() <= neon::SynthVoiceBank::kVoices);
  CHECK(bank.active_voices() == neon::SynthVoiceBank::kVoices);
  CHECK(render_peak(bank, 512) > 0.0f);

  bank.all_notes_off();
  for (int i = 0; i < 40; ++i) {
    render_peak(bank, 4410);
  }
  CHECK(bank.active_voices() == 0);
}

TEST_CASE("SynthVoiceBank: output stays inside full scale with every voice on") {
  neon::SynthVoiceBank bank;
  bank.reset(kRate);
  for (int n = 0; n < neon::SynthVoiceBank::kVoices; ++n) {
    bank.note_on(static_cast<uint8_t>(36 + n), 127);
  }
  std::vector<float> buf(44100, 0.0f);
  bank.render(buf.data(), 44100);
  for (float s : buf) {
    CHECK(std::fabs(s) <= 1.0f);
  }
}

TEST_CASE("SynthVoiceBank: every patch renders something") {
  for (uint8_t p = 0; p < neon::SynthVoiceBank::kPatchCount; ++p) {
    neon::SynthVoiceBank bank;
    bank.reset(kRate);
    bank.set_patch(p);
    CHECK(bank.patch() == p);
    bank.note_on(69, 100);
    CHECK(render_peak(bank, 8820) > 0.01f);
  }
}

TEST_CASE("SynthVoiceBank: pitch tracks the note number") {
  // An octave up doubles the zero-crossing rate.
  auto crossings = [](uint8_t note) {
    neon::SynthVoiceBank bank;
    bank.reset(kRate);
    bank.set_patch(2);  // sine: cleanest to count
    bank.note_on(note, 100);
    std::vector<float> buf(kRate, 0.0f);
    bank.render(buf.data(), kRate);
    uint32_t n = 0;
    for (size_t i = kRate / 4; i < buf.size(); ++i) {
      if (buf[i - 1] <= 0.0f && buf[i] > 0.0f) ++n;
    }
    return n;
  };
  const uint32_t a3 = crossings(57);
  const uint32_t a4 = crossings(69);
  CHECK(a4 > a3 * 19 / 10);
  CHECK(a4 < a3 * 21 / 10);
}
