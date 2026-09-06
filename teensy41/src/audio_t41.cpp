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
#include "neon/audio/onset_detector.hpp"
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
// One block at 44.1 kHz / 128. Period measurement is delay-invariant.
constexpr int64_t kAdcLatencyUs =
    (static_cast<int64_t>(kBlockFrames) * 1000000) / kRate;

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
neon::OnsetDetector g_detector;
neon::PulseRender g_pulse;
uint8_t g_follow_was = 0;
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
float g_in_l[kBlockFrames];
float g_in_r[kBlockFrames];

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

bool source_needed(const neon::AudioEngineConfig& cfg, neon::AudioRole role,
                   bool in_mix) {
  if (cfg.role_l == role || cfg.role_r == role) {
    return true;
  }
  return in_mix && (cfg.role_l == neon::AudioRole::kMix ||
                    cfg.role_r == neon::AudioRole::kMix);
}

void run_follow(const neon::AudioEngineConfig& cfg, uint64_t first_frame,
                uint32_t n, const float* L, const float* R) {
  if (cfg.follow_enabled != 0 && g_follow_was == 0) {
    g_detector.reset(kRate);
  }
  g_follow_was = cfg.follow_enabled;
  g_detector.set_sensitivity(cfg.follow_sensitivity);
  if (cfg.follow_enabled == 0 || L == nullptr || R == nullptr) {
    return;
  }
  const int64_t t0_adc = g_clock.us_at_frame(first_frame) - kAdcLatencyUs;
  const uint64_t us_per_frame_q32 = g_clock.us_per_frame_q32();
  const bool click_can_leak =
      cfg.enabled != 0 && cfg.metro_enabled != 0 &&
      cfg.follow_enabled != 0 &&
      source_needed(cfg, neon::AudioRole::kMetronome, /*in_mix=*/true);
  if (click_can_leak && g_click.last_onsets() > 0) {
    const int64_t click_us =
        t0_adc + static_cast<int64_t>(
                     (static_cast<uint64_t>(g_click.last_onset_frame()) *
                      us_per_frame_q32) >>
                     32);
    g_detector.note_click(click_us);
  }
  neon::OnsetEvent hits[8];
  const uint32_t nh =
      g_detector.process(L, R, n, t0_adc, us_per_frame_q32, hits, 8);
  for (uint32_t i = 0; i < nh; ++i) {
    onset_queue_push(hits[i]);
  }
}

class NeonAudioSource : public AudioStream {
 public:
  NeonAudioSource() : AudioStream(2, inputQueueArray) {}

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

    const neon::BeatWindow window =
        neon::beat_window(g_staged.tl, t0, t1, kBlockFrames);

    // Click before the detector so last_onsets() is this block (ESP order).
    neon::MixSources src;
    std::memset(g_metro, 0, sizeof(g_metro));
    const bool click_sounding = g_click.voice_active();
    if (cfg.enabled != 0 && (cfg.metro_enabled != 0 || click_sounding)) {
      g_click.render(window, g_metro, kBlockFrames);
      src.metro = g_metro;
    }

    audio_block_t* inl = receiveReadOnly(0);
    audio_block_t* inr = receiveReadOnly(1);
    constexpr float kScale = 1.0f / 32768.0f;
    for (uint32_t i = 0; i < kBlockFrames; ++i) {
      g_in_l[i] = inl != nullptr ? static_cast<float>(inl->data[i]) * kScale
                                 : 0.0f;
      g_in_r[i] = inr != nullptr ? static_cast<float>(inr->data[i]) * kScale
                                 : 0.0f;
    }
    run_follow(cfg, first_frame, kBlockFrames, g_in_l, g_in_r);
    if (inl != nullptr) {
      release(inl);
    }
    if (inr != nullptr) {
      release(inr);
    }

    if (cfg.enabled == 0) {
      g_running = false;
      g_peak_l = 0;
      g_peak_r = 0;
      audio_block_t* left = allocate();
      audio_block_t* right = allocate();
      if (left != nullptr && right != nullptr) {
        std::memset(left->data, 0, sizeof(left->data));
        std::memset(right->data, 0, sizeof(right->data));
        transmit(left, 0);
        transmit(right, 1);
      }
      if (left != nullptr) {
        release(left);
      }
      if (right != nullptr) {
        release(right);
      }
      return;
    }
    g_running = true;

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

 private:
  audio_block_t* inputQueueArray[2] = {};
};

// Audio graph. Constructed at static init, silent until init() enables
// the codec and the config enables the engine. Input is attached so
// follow can run with AUDIO off (silence still goes out).
AudioInputI2S g_i2s_in;
NeonAudioSource g_source;
AudioOutputI2S g_i2s;
AudioConnection g_in_l(g_i2s_in, 0, g_source, 0);
AudioConnection g_in_r(g_i2s_in, 1, g_source, 1);
AudioConnection g_conn_l(g_source, 0, g_i2s, 0);
AudioConnection g_conn_r(g_source, 1, g_i2s, 1);
AudioControlSGTL5000 g_codec;

}  // namespace

void init() {
  AudioMemory(16);
  g_clock.reset(kRate);
  g_click.reset(kRate);
  g_pulse.reset(kRate);
  g_detector.reset(kRate);
  // No shield present is fine: enable() just fails and the I2S pins
  // wiggle for any external DAC instead.
  g_codec.enable();
  g_codec.volume(0.6f);
  g_codec.inputSelect(AUDIO_INPUT_LINEIN);
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
