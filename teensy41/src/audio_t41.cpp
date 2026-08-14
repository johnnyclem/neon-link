#include "audio_t41.h"

#include <Arduino.h>
#include <Audio.h>

#include <cstring>

#include "app_state/audio_bus.h"
#include "app_state/config_store.h"
#include "app_state/timeline_bus.h"
#include "neon/audio/beat_window.hpp"
#include "neon/audio/click.hpp"
#include "neon/audio/mixer.hpp"
#include "neon/audio/pulse_render.hpp"
#include "neon/audio/sample_clock.hpp"

#include "irq_lock_t41.h"
#include "timebase_t41.h"

namespace audioeng {
namespace {

constexpr uint32_t kBlockFrames = AUDIO_BLOCK_SAMPLES;  // 128
constexpr uint32_t kRate = 44100;
// I2S FIFO + the library's queueing between update() and the wire.
constexpr int64_t kDacLatencyUs = 6000;

// --- main-loop -> ISR staging (written IRQ-masked, read at block start)
struct Staged {
  neon::TimelineSnapshot tl;
  neon::EngineConfig eng;
  neon::AudioEngineConfig cfg;
  uint32_t tl_gen = 0;
  uint32_t eng_gen = 0;
};
Staged g_staged;

// --- ISR-side state ---------------------------------------------------
neon::SampleClock g_clock;
neon::ClickSynth g_click;
neon::PulseRender g_pulse;
uint64_t g_frames = 0;
int64_t g_prev_t1 = 0;
uint32_t g_seen_tl_gen = 0;
uint32_t g_seen_eng_gen = 0;
bool g_have_timeline = false;
bool g_last_playing = false;

float g_metro[kBlockFrames];
float g_tap[kBlockFrames];
float g_out_l[kBlockFrames];
float g_out_r[kBlockFrames];

// Meters back to the main loop (plain volatiles; races are benign).
volatile uint16_t g_peak_l = 0;
volatile uint16_t g_peak_r = 0;
volatile uint32_t g_blocks = 0;
volatile bool g_running = false;

neon::ClickConfig click_config(const neon::AudioEngineConfig& cfg) {
  neon::ClickConfig c;
  c.enabled = cfg.metro_enabled != 0;
  c.sound = cfg.metro_sound;
  c.gain = cfg.metro_gain;
  c.accent = cfg.metro_accent != 0;
  return c;
}

neon::MixerConfig mixer_config(const neon::AudioEngineConfig& cfg,
                               bool click_sounding) {
  neon::MixerConfig m;
  m.role_l = cfg.role_l;
  m.role_r = cfg.role_r;
  m.metro_gain = cfg.metro_gain;
  m.amy_gain = cfg.amy_gain;
  m.linein_gain = 0;
  m.sub_gain = cfg.la_sub_gain;
  m.metro_enabled = cfg.metro_enabled != 0 || click_sounding;
  m.amy_enabled = false;
  return m;
}

bool role_selected(const neon::AudioEngineConfig& cfg, neon::AudioRole role) {
  return cfg.role_l == role || cfg.role_r == role;
}

class NeonAudioSource : public AudioStream {
 public:
  NeonAudioSource() : AudioStream(0, nullptr) {}

  void update() override {
    const uint64_t first_frame = g_frames;
    g_frames += kBlockFrames;
    g_clock.update(t41_now_us(), g_frames);

    // Staging is written IRQ-masked on the main thread, so inside this
    // ISR it is stable for the whole block.
    const neon::AudioEngineConfig& cfg = g_staged.cfg;

    if (g_staged.eng_gen != g_seen_eng_gen) {
      g_seen_eng_gen = g_staged.eng_gen;
      g_pulse.set_config(g_staged.eng);
      g_have_timeline = false;
    }
    g_click.set_config(click_config(cfg));

    const int64_t t0 = g_clock.us_at_frame(first_frame) + kDacLatencyUs;
    const int64_t t1 =
        g_clock.us_at_frame(first_frame + kBlockFrames) + kDacLatencyUs;

    const bool timeline_moved = g_staged.tl_gen != g_seen_tl_gen;
    if (timeline_moved) {
      g_seen_tl_gen = g_staged.tl_gen;
    }
    if ((timeline_moved || !g_have_timeline) && t1 > t0) {
      g_pulse.retime(g_staged.tl, t0);
      if (g_last_playing && g_staged.tl.playing == 0) {
        g_click.fade_out();
      }
      g_last_playing = g_staged.tl.playing != 0;
      g_have_timeline = true;
      g_prev_t1 = t0;
    }

    if (cfg.enabled == 0) {
      g_running = false;
      g_peak_l = 0;
      g_peak_r = 0;
      // Transmit nothing: the output amps get silence.
      return;
    }
    g_running = true;

    const neon::BeatWindow window =
        neon::beat_window(g_staged.tl, t0, t1, kBlockFrames);

    neon::MixSources src;
    std::memset(g_metro, 0, sizeof(g_metro));
    const bool click_sounding = g_click.voice_active();
    if (cfg.metro_enabled != 0 || click_sounding) {
      g_click.render(window, g_metro, kBlockFrames);
      src.metro = g_metro;
    }

    // The pulse window stays contiguous whether or not a jack listens
    // (same contract as the ESP audio task).
    g_pulse.begin_block(g_prev_t1 > t0 ? g_prev_t1 : t0, t1, kBlockFrames);
    g_prev_t1 = t1;
    // One shared tap buffer: at most one solo pulse role per physical
    // channel, and the mixer reads each pointer before the next render.
    if (role_selected(cfg, neon::AudioRole::kClock)) {
      std::memset(g_tap, 0, sizeof(g_tap));
      g_pulse.render_channel(neon::kChClk1, 0.9f, g_tap, kBlockFrames);
      src.clock = g_tap;
    } else if (role_selected(cfg, neon::AudioRole::kReset)) {
      std::memset(g_tap, 0, sizeof(g_tap));
      g_pulse.render_channel(neon::kChReset, 0.9f, g_tap, kBlockFrames);
      src.reset = g_tap;
    } else if (role_selected(cfg, neon::AudioRole::kRun)) {
      std::memset(g_tap, 0, sizeof(g_tap));
      g_pulse.render_channel(neon::kChRun, 0.9f, g_tap, kBlockFrames);
      src.run = g_tap;
    }

    neon::mix_block(mixer_config(cfg, click_sounding), src, kBlockFrames,
                    g_out_l, g_out_r);

    audio_block_t* left = allocate();
    audio_block_t* right = allocate();
    if (left != nullptr && right != nullptr) {
      int16_t interleaved[2];
      for (uint32_t i = 0; i < kBlockFrames; ++i) {
        neon::float_to_int16(&g_out_l[i], &g_out_r[i], 1, interleaved);
        left->data[i] = interleaved[0];
        right->data[i] = interleaved[1];
      }
      transmit(left, 0);
      transmit(right, 1);
    }
    if (left != nullptr) {
      release(left);
    }
    if (right != nullptr) {
      release(right);
    }

    g_peak_l = neon::peak_meter(g_out_l, kBlockFrames, g_peak_l);
    g_peak_r = neon::peak_meter(g_out_r, kBlockFrames, g_peak_r);
    g_blocks = g_blocks + 1;
  }
};

// Audio graph. Constructed at static init, silent until init() enables
// the codec and the config enables the engine.
NeonAudioSource g_source;
AudioOutputI2S g_i2s;
AudioConnection g_conn_l(g_source, 0, g_i2s, 0);
AudioConnection g_conn_r(g_source, 1, g_i2s, 1);
AudioControlSGTL5000 g_codec;

}  // namespace

void init() {
  AudioMemory(16);
  g_clock.reset(kRate);
  g_click.reset(kRate);
  g_pulse.reset(kRate);
  // No shield present is fine: enable() just fails and the I2S pins
  // wiggle for any external DAC instead.
  g_codec.enable();
  g_codec.volume(0.6f);
}

void poll(int64_t now_us) {
  // Stage the buses for the ISR under an IRQ mask (sub-microsecond for
  // these struct sizes at 600 MHz).
  neon::TimelineSnapshot tl;
  const uint32_t tl_v = timeline_bus().read(tl);
  neon::EngineConfig eng;
  const uint32_t eng_v = engine_config_bus().read(eng);
  neon::AudioEngineConfig acfg;
  audio_config_bus().read(acfg);

  const uint32_t primask = irq_save();
  g_staged.tl = tl;
  g_staged.eng = eng;
  g_staged.cfg = acfg;
  g_staged.tl_gen = tl_v;
  g_staged.eng_gen = eng_v;
  irq_restore(primask);

  // Publish meters for /api/status and the panel.
  static int64_t next_status_us = 0;
  if (now_us >= next_status_us) {
    next_status_us = now_us + 250000;
    neon::AudioStatus st{};
    st.running = g_running ? 1 : 0;
    st.peak_l = g_peak_l;
    st.peak_r = g_peak_r;
    audio_status_bus().publish(st);
  }
}

}  // namespace audioeng
