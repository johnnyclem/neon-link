// The synth seam over shorepine's AMY (third_party/amy, MIT).
//
// AMY renders in blocks of AMY_BLOCK_SIZE frames at AMY_SAMPLE_RATE into
// its own interleaved int16 buffer; we pull whole AMY blocks, keep the
// remainder, and hand the audio task exactly the frames it asked for. AMY
// is initialised at the engine rate (48 kHz on the AMYboard).
//
// Voices are clamped: the block budget at 5.3 ms is what it is, and a
// synth that overruns it takes the clock outputs with it.

#include "amysynth/synth.h"

#include <cstring>

extern "C" {
#include "amy.h"
}

#include "sdkconfig.h"

namespace amysynth {

namespace {

#ifdef CONFIG_NEON_AUDIO_AMY_VOICES
constexpr uint16_t kVoices = CONFIG_NEON_AUDIO_AMY_VOICES;
#else
constexpr uint16_t kVoices = 6;
#endif

constexpr uint32_t kMaxPending = 512;

bool g_started = false;
uint8_t g_patch = 0;
uint8_t g_note_voice[128];  // MIDI note -> AMY oscillator, 0xff = not sounding

// Whatever AMY produced beyond the last request.
int16_t g_pending[kMaxPending * 2];
uint32_t g_pending_frames = 0;

uint16_t patch_number(uint8_t patch) {
  // Four presets from AMY's built-in patch bank, in the same order the
  // config's amy_patch index implies.
  static const uint16_t kPatches[4] = {0, 1, 2, 3};
  return kPatches[patch % 4];
}

}  // namespace

void init(uint32_t /*sample_rate*/, uint16_t /*block_frames*/) {
  if (g_started) {
    return;
  }
  amy_config_t cfg = amy_default_config();
  cfg.features.chorus = 0;
  cfg.features.reverb = 0;
  cfg.features.echo = 0;
  cfg.max_voices = kVoices;
  amy_start(cfg);
  std::memset(g_note_voice, 0xff, sizeof(g_note_voice));
  g_started = true;
  set_patch(g_patch);
}

void set_patch(uint8_t patch) {
  g_patch = static_cast<uint8_t>(patch % 4);
  if (!g_started) {
    return;
  }
  amy_event e = amy_default_event();
  e.load_patch = patch_number(g_patch);
  for (uint16_t v = 0; v < kVoices; ++v) {
    e.voices[0] = v;
    amy_add_event(&e);
  }
}

void note_on(uint8_t note, uint8_t velocity) {
  if (!g_started || note > 127) {
    return;
  }
  if (velocity == 0) {
    note_off(note);
    return;
  }
  const uint8_t voice = static_cast<uint8_t>(note % kVoices);
  g_note_voice[note] = voice;
  amy_event e = amy_default_event();
  e.voices[0] = voice;
  e.midi_note = note;
  e.velocity = static_cast<float>(velocity) / 127.0f;
  amy_add_event(&e);
}

void note_off(uint8_t note) {
  if (!g_started || note > 127 || g_note_voice[note] == 0xff) {
    return;
  }
  amy_event e = amy_default_event();
  e.voices[0] = g_note_voice[note];
  e.velocity = 0.0f;
  amy_add_event(&e);
  g_note_voice[note] = 0xff;
}

void all_notes_off() {
  for (int n = 0; n < 128; ++n) {
    note_off(static_cast<uint8_t>(n));
  }
}

void render(float* out, uint32_t frames) {
  if (!g_started || out == nullptr || frames == 0) {
    return;
  }
  constexpr float kScale = 1.0f / 32768.0f;
  uint32_t done = 0;

  // Anything left over from the previous call first.
  if (g_pending_frames != 0) {
    const uint32_t n = g_pending_frames < frames ? g_pending_frames : frames;
    for (uint32_t i = 0; i < n; ++i) {
      out[i] += static_cast<float>(g_pending[2 * i]) * kScale;
    }
    done = n;
    const uint32_t left = g_pending_frames - n;
    if (left != 0) {
      std::memmove(g_pending, g_pending + n * 2,
                   static_cast<size_t>(left) * 2 * sizeof(int16_t));
    }
    g_pending_frames = left;
  }

  while (done < frames) {
    const int16_t* block = amy_simple_fill_buffer();
    if (block == nullptr) {
      return;
    }
    const uint32_t have = AMY_BLOCK_SIZE;
    const uint32_t want = frames - done;
    const uint32_t n = have < want ? have : want;
    for (uint32_t i = 0; i < n; ++i) {
      out[done + i] += static_cast<float>(block[i * AMY_NCHANS]) * kScale;
    }
    done += n;
    if (have > n && have - n <= kMaxPending) {
      const uint32_t left = have - n;
      for (uint32_t i = 0; i < left; ++i) {
        g_pending[2 * i] = block[(n + i) * AMY_NCHANS];
        g_pending[2 * i + 1] = block[(n + i) * AMY_NCHANS];
      }
      g_pending_frames = left;
    }
  }
}

uint32_t active_voices() { return kVoices; }

bool using_amy() { return true; }

}  // namespace amysynth
