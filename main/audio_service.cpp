// Core 1: the audio render loop, and its core-0 control half.
//
// The render task is paced by the I2S DMA: write_block() blocks until
// there is room, so there is no timer and no sleep in the loop. Every
// block it asks the SampleClock when the frames it is about to hand over
// will actually leave the converter, turns that into a beat window from
// the Link timeline snapshot, and renders every source against that
// window. Nothing here consults the Link session, allocates, or touches
// the network.
//
// The control half (core 0) owns everything with a socket behind it:
// creating and destroying Link Audio sinks, subscribing, and discovery for
// the editor. See docs/AUDIOLINK.md §5.

#include <atomic>
#include <cstdio>
#include <cstring>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "ablink/audio.hpp"
#include "amysynth/synth.h"
#include "app_state/audio_bus.h"
#include "app_state/config_store.h"
#include "app_state/timeline_bus.h"
#include "board_pins.h"
#include "halesp/i2s_audio.hpp"
#include "netman/net_manager.h"
#include "neon/audio/beat_window.hpp"
#include "neon/audio/click.hpp"
#include "neon/audio/jitter_buffer.hpp"
#include "neon/audio/lpf.hpp"
#include "neon/audio/mixer.hpp"
#include "neon/audio/pulse_render.hpp"
#include "neon/audio/sample_clock.hpp"
#include "neon/telemetry/csv.hpp"
#include "tasks.h"
#include "wifi.h"

#if CONFIG_NEON_AUDIO

namespace {

const char* kTag = "audio_svc";

constexpr uint32_t kSampleRate = 48000;
constexpr uint16_t kBlockFrames = 256;
constexpr uint8_t kDmaDesc = 8;
constexpr uint32_t kMaxRxFrames = 512;

// Analog latency of the DAC's reconstruction filter, on top of the DMA
// latency the SampleClock already accounts for. Measured against the CV
// outputs on hardware (docs/AUDIOLINK.md PR8); zero until it is.
constexpr int32_t kDacLatencyUs = 0;

// JitterBuffer caps its fill target at HALF the ring (the servo needs
// somewhere to go), so honoring the 800 ms jitter maximum takes a ring
// twice that deep: 131072 frames is ~2.7 s of stereo at 48 kHz (512 KB of
// PSRAM), for a usable target of ~1.36 s. The previous 32768 silently
// truncated every la_jitter_ms above ~341 ms — the 650 ms the busy-LAN
// tuning session thought it was running with was never actually applied.
constexpr uint32_t kJitterRingFrames = 131072;

// Per-source render buffers. Static rather than stack: this task's stack
// would have to be 8 KB bigger for no reason.
float g_metro[kBlockFrames];
float g_clock[kBlockFrames];
float g_reset[kBlockFrames];
float g_run[kBlockFrames];
float g_amy[kBlockFrames];
float g_link_l[kBlockFrames];
float g_link_r[kBlockFrames];
float g_line_l[kBlockFrames];
float g_line_r[kBlockFrames];
float g_out_l[kBlockFrames];
float g_out_r[kBlockFrames];
int16_t g_out_i16[kBlockFrames * 2];
int16_t g_in_i16[kBlockFrames * 2];
int16_t g_mono_i16[kBlockFrames];
int16_t g_rx_i16[kMaxRxFrames * 2];

neon::ClickSynth g_click;
neon::PulseRender g_pulse;
neon::JitterBuffer g_jitter;
neon::GistFilter g_gist_l;
neon::GistFilter g_gist_r;

// Sink handles, written by the control task and read by the render task.
std::atomic<int> g_sink_mix{-1};
std::atomic<int> g_sink_linein{-1};

void clear(float* buf) {
  for (uint32_t i = 0; i < kBlockFrames; ++i) {
    buf[i] = 0.0f;
  }
}

// Is `role` on either output, or folded into a mix that is?
bool source_needed(const neon::AudioEngineConfig& cfg, neon::AudioRole role,
                   bool in_mix) {
  if (cfg.role_l == role || cfg.role_r == role) {
    return true;
  }
  return in_mix && (cfg.role_l == neon::AudioRole::kMix ||
                    cfg.role_r == neon::AudioRole::kMix);
}

neon::MixerConfig mixer_config(const neon::AudioEngineConfig& cfg,
                               bool click_sounding) {
  neon::MixerConfig m;
  m.role_l = cfg.role_l;
  m.role_r = cfg.role_r;
  m.metro_gain = cfg.metro_gain;
  m.amy_gain = cfg.amy_gain;
  m.linein_gain = cfg.linein_monitor_gain;
  m.sub_gain = cfg.la_sub_gain;
  // A click switched off mid-envelope still has to reach the output, or
  // the fade is a fade into a muted bus — which is the pop it exists to
  // prevent.
  m.metro_enabled = cfg.metro_enabled != 0 || click_sounding;
  m.amy_enabled = cfg.amy_enabled != 0;
  return m;
}

neon::ClickConfig click_config(const neon::AudioEngineConfig& cfg) {
  neon::ClickConfig c;
  c.enabled = cfg.metro_enabled != 0;
  c.sound = cfg.metro_sound;
  c.gain = cfg.metro_gain;
  c.accent = cfg.metro_accent != 0;
  // The unit often shows STOP while Live is playing (start/stop sync).
  // Gating the click on transport left LINE OUT silent for every mix
  // that depended on the metronome. Tick the Link beat grid instead.
  c.follow_transport = false;
  return c;
}

void drain_synth_queue() {
  SynthEvent ev;
  while (synth_queue_pop(&ev)) {
    if (ev.all_off != 0) {
      amysynth::all_notes_off();
    } else if (ev.on != 0) {
      amysynth::note_on(ev.note, ev.velocity);
    } else {
      amysynth::note_off(ev.note);
    }
  }
}

// Moves whatever the network delivered into the jitter buffer. Cheap: a
// pop from a lock-free ring and a memcpy, both bounded. The guard must
// clear a whole WiFi burst faster than the network can refill the block
// ring, or the ring overflows and drops audio the jitter buffer never
// sees — 32 pops per 5.3 ms block outruns any sender rate the protocol
// allows while still bounding the loop.
void drain_link_audio(hal::ILinkAudio& la) {
  for (int guard = 0; guard < 32; ++guard) {
    neon::AudioBlockInfo info;
    uint8_t channels = 2;
    uint32_t rate = 0;
    const uint32_t frames =
        la.source_read(g_rx_i16, kMaxRxFrames, rate, channels,
                       info.begin_beat_q32, info.end_beat_q32);
    if (frames == 0) {
      return;
    }
    info.frames = frames;
    info.sample_rate = rate;
    info.channels = channels;
    g_jitter.push(info, g_rx_i16);
  }
}

void audio_task(void*) {
  halesp::I2sAudio& io = halesp::i2s_audio();
  io.set_pins(halesp::I2sPins{kPinI2sMclk, kPinI2sBclk, kPinI2sLrclk,
                              kPinI2sDout, kPinI2sDin});

  hal::AudioIoConfig io_cfg;
  io_cfg.sample_rate = kSampleRate;
  io_cfg.block_frames = kBlockFrames;
  io_cfg.dma_desc = kDmaDesc;
#if CONFIG_NEON_AUDIO_INPUT
  io_cfg.enable_input = true;
#endif

  if (!io.start(io_cfg)) {
    ESP_LOGW(kTag, "I2S unavailable (check the I2S pin map); audio is off");
    neon::AudioStatus status;
    audio_status_bus().publish(status);
    vTaskDelete(nullptr);
    return;
  }

  neon::SampleClock clock;
  clock.reset(kSampleRate);
  g_click.reset(kSampleRate);
  g_pulse.reset(kSampleRate);
  g_gist_l.set_rate(kSampleRate);
  g_gist_r.set_rate(kSampleRate);
  amysynth::init(kSampleRate, kBlockFrames);

  int16_t* jitter_storage = static_cast<int16_t*>(heap_caps_malloc(
      static_cast<size_t>(kJitterRingFrames) * 2 * sizeof(int16_t),
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (jitter_storage == nullptr) {
    jitter_storage = static_cast<int16_t*>(heap_caps_malloc(
        static_cast<size_t>(kJitterRingFrames) * 2 * sizeof(int16_t),
        MALLOC_CAP_8BIT));
  }
  if (jitter_storage != nullptr) {
    g_jitter.init(jitter_storage, kJitterRingFrames, kSampleRate);
  } else {
    ESP_LOGW(kTag, "no memory for the receive buffer; subscriptions disabled");
  }

  hal::ILinkAudio& link_audio = ablink::link_audio();

  neon::AudioEngineConfig cfg;
  uint32_t cfg_version = audio_config_bus().read(cfg);
  g_click.set_config(click_config(cfg));
  amysynth::set_patch(cfg.amy_patch);
  g_jitter.configure(cfg.la_jitter_ms);

  neon::EngineConfig eng;
  uint32_t eng_version = engine_config_bus().read(eng);
  g_pulse.set_config(eng);

  neon::TimelineSnapshot tl{};
  uint32_t tl_version = timeline_bus().read(tl);
  bool have_timeline = false;
  bool last_playing = tl.playing != 0;

  neon::AudioStatus status;
  status.running = 1;
  uint32_t status_countdown = 0;
  uint32_t underruns = 0;
  int64_t prev_t1 = 0;

  ESP_LOGI(kTag, "audio task running: %u Hz, %u frames/block; gist %s",
           static_cast<unsigned>(kSampleRate),
           static_cast<unsigned>(kBlockFrames),
           cfg.la_fullband ? "bypass" : "120 Hz-5 kHz");

  for (;;) {
    // --- clocks and configuration -------------------------------------
    int64_t mark_us = 0;
    uint64_t mark_frames = 0;
    while (io.dma_mark(mark_us, mark_frames)) {
      clock.update(mark_us, mark_frames);
    }

    const uint32_t new_cfg_version = audio_config_bus().version();
    if (new_cfg_version != cfg_version) {
      const bool metro_was_on = cfg.metro_enabled != 0;
      const uint8_t fullband_was = cfg.la_fullband;
      cfg_version = audio_config_bus().read(cfg);
      if (metro_was_on && cfg.metro_enabled == 0) {
        g_click.fade_out();
      }
      g_click.set_config(click_config(cfg));
      amysynth::set_patch(cfg.amy_patch);
      g_jitter.configure(cfg.la_jitter_ms);
      if (cfg.la_fullband != fullband_was) {
        g_gist_l.reset();
        g_gist_r.reset();
        ESP_LOGI(kTag, "gist %s", cfg.la_fullband ? "bypass" : "120 Hz-5 kHz");
      }
    }
    const uint32_t new_eng_version = engine_config_bus().version();
    if (new_eng_version != eng_version) {
      eng_version = engine_config_bus().read(eng);
      g_pulse.set_config(eng);
      have_timeline = false;  // force a retime against the new config
    }

    drain_synth_queue();
    drain_link_audio(link_audio);

    // --- where this block lands on the session grid --------------------
    const uint64_t first_frame = io.frames_written();
    const int64_t t0 = clock.us_at_frame(first_frame) + kDacLatencyUs;
    const int64_t t1 =
        clock.us_at_frame(first_frame + kBlockFrames) + kDacLatencyUs;

    const uint32_t new_tl_version = timeline_bus().version();
    const bool timeline_moved = new_tl_version != tl_version;
    if (timeline_moved) {
      tl_version = timeline_bus().read(tl);
    }
    if ((timeline_moved || !have_timeline) && t1 > t0) {
      // A new snapshot re-anchors the pulse grid at this block boundary;
      // the click is stateless per block and needs no help. A transport
      // stop fades whatever is sounding rather than cutting it.
      g_pulse.retime(tl, t0);
      if (last_playing && tl.playing == 0) {
        g_click.fade_out();
      }
      last_playing = tl.playing != 0;
      have_timeline = true;
      prev_t1 = t0;
    }

    const neon::BeatWindow window = neon::beat_window(tl, t0, t1, kBlockFrames);

    // --- sources -------------------------------------------------------
    const bool want_metro =
        cfg.metro_enabled != 0 &&
        source_needed(cfg, neon::AudioRole::kMetronome, /*in_mix=*/true);
    const bool want_clock = source_needed(cfg, neon::AudioRole::kClock, false);
    const bool want_reset = source_needed(cfg, neon::AudioRole::kReset, false);
    const bool want_run = source_needed(cfg, neon::AudioRole::kRun, false);
    const bool want_amy =
        cfg.amy_enabled != 0 &&
        source_needed(cfg, neon::AudioRole::kAmy, /*in_mix=*/true);
    const bool want_link =
        source_needed(cfg, neon::AudioRole::kLinkIn, /*in_mix=*/true);
    const bool want_line =
        cfg.linein_monitor_gain != 0 &&
        source_needed(cfg, neon::AudioRole::kLineIn, /*in_mix=*/true);
    const bool publish_linein = g_sink_linein.load(std::memory_order_relaxed) >= 0;

    neon::MixSources src;

    clear(g_metro);
    const bool click_sounding = g_click.voice_active();
    if (want_metro || click_sounding) {
      g_click.render(window, g_metro, kBlockFrames);
      src.metro = g_metro;
    }

    // The pulse window must stay contiguous whether or not anyone is
    // listening, or the engine's edge stream desynchronises the moment a
    // role is switched on.
    g_pulse.begin_block(prev_t1 > t0 ? prev_t1 : t0, t1, kBlockFrames);
    prev_t1 = t1;
    if (want_clock) {
      clear(g_clock);
      g_pulse.render_channel(neon::kChClk1, 0.9f, g_clock, kBlockFrames);
      src.clock = g_clock;
    }
    if (want_reset) {
      clear(g_reset);
      g_pulse.render_channel(neon::kChReset, 0.9f, g_reset, kBlockFrames);
      src.reset = g_reset;
    }
    if (want_run) {
      clear(g_run);
      g_pulse.render_channel(neon::kChRun, 0.9f, g_run, kBlockFrames);
      src.run = g_run;
    }

    clear(g_amy);
    if (want_amy) {
      amysynth::render(g_amy, kBlockFrames);
      src.amy = g_amy;
    }

    if (want_link && jitter_storage != nullptr) {
      g_jitter.pull(kBlockFrames, g_link_l, g_link_r);
      if (cfg.la_fullband == 0) {
        g_gist_l.process(g_link_l, kBlockFrames);
        g_gist_r.process(g_link_r, kBlockFrames);
      }
      src.link_in_l = g_link_l;
      src.link_in_r = g_link_r;
    } else {
      g_gist_l.reset();
      g_gist_r.reset();
    }

    const bool have_input = io.read_block(g_in_i16);
    if (have_input && (want_line || publish_linein)) {
      neon::int16_to_float(g_in_i16, 2, kBlockFrames, g_line_l, g_line_r);
      if (want_line) {
        src.line_in_l = g_line_l;
        src.line_in_r = g_line_r;
      }
    }

    // --- mix, publish, play --------------------------------------------
    if (cfg.enabled == 0) {
      clear(g_out_l);
      clear(g_out_r);
    } else {
      neon::mix_block(mixer_config(cfg, click_sounding), src, kBlockFrames,
                      g_out_l, g_out_r);
    }
    neon::float_to_int16(g_out_l, g_out_r, kBlockFrames, g_out_i16);

    const int sink_mix = g_sink_mix.load(std::memory_order_relaxed);
    if (sink_mix >= 0 && window.valid) {
      if (cfg.la_publish_mono != 0) {
        neon::stereo_to_mono_i16(g_out_i16, kBlockFrames, g_mono_i16);
        link_audio.sink_write(sink_mix, g_mono_i16, kBlockFrames,
                              window.begin_q32, window.end_q32);
      } else {
        link_audio.sink_write(sink_mix, g_out_i16, kBlockFrames,
                              window.begin_q32, window.end_q32);
      }
    }
    const int sink_line = g_sink_linein.load(std::memory_order_relaxed);
    if (sink_line >= 0 && have_input && window.valid) {
      if (cfg.la_publish_mono != 0) {
        neon::stereo_to_mono_i16(g_in_i16, kBlockFrames, g_mono_i16);
        link_audio.sink_write(sink_line, g_mono_i16, kBlockFrames,
                              window.begin_q32, window.end_q32);
      } else {
        link_audio.sink_write(sink_line, g_in_i16, kBlockFrames,
                              window.begin_q32, window.end_q32);
      }
    }

    if (!io.write_block(g_out_i16)) {
      ++underruns;
      // The driver is not taking blocks; yield rather than spin the core.
      vTaskDelay(1);
    }

    // --- status ---------------------------------------------------------
    status.peak_l = neon::peak_meter(g_out_l, kBlockFrames, status.peak_l);
    status.peak_r = neon::peak_meter(g_out_r, kBlockFrames, status.peak_r);
    if (status_countdown-- == 0) {
      status_countdown = 16;  // ~46 ms
      status.running = 1;
      status.underruns = underruns + io.write_failures();
      status.publishing =
          (sink_mix >= 0 && link_audio.sink_has_subscribers(sink_mix)) ||
                  (sink_line >= 0 && link_audio.sink_has_subscribers(sink_line))
              ? 1
              : 0;
      status.subscribers = link_audio.subscriber_count();
      status.sub_state = static_cast<uint8_t>(g_jitter.state());
      status.sub_dropped = g_jitter.dropped();
      status.sub_rate = g_jitter.sender_rate();
      status.fill_frames = g_jitter.fill_frames();
      status.fill_ms = (status.fill_frames * 1000u) / kSampleRate;
      status.clock_ppm = clock.ppm();
      status.clock_residual_us = static_cast<int32_t>(clock.residual_us());
      status.rx_dropped = link_audio.source_dropped();
      status.jit_underruns = g_jitter.underruns();
      status.tx_dropped = link_audio.sink_dropped();
      status.trim_ppm = g_jitter.trim_ppm();
      status.concealed = g_jitter.concealed();
      status.rx_high_water = link_audio.rx_high_water();
      status.i2s_write_failures = io.write_failures();
      status.heap_free_internal =
          static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
      status.heap_free_psram =
          static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
      status.rssi = neon_wifi_rssi();
      status.priority_profile = cfg.priority_profile;
      status.req_jitter_ms = static_cast<uint16_t>(g_jitter.jitter_ms());
      status.eff_jitter_ms =
          static_cast<uint16_t>(g_jitter.effective_jitter_ms());
      audio_status_bus().publish(status);
    }
  }
}

// ---- control half (core 0) ---------------------------------------------

// Publish flags and the subscription are restart-scoped: they involve
// sockets, so they are applied here and never from the audio task.
void apply_publish_state(hal::ILinkAudio& la, const neon::Config& cfg) {
  const bool want_mix = cfg.audio.la_publish_mix != 0;
  const bool want_line = cfg.audio.la_publish_linein != 0;
  const uint8_t channels = cfg.audio.la_publish_mono != 0 ? 1 : 2;

  int mix = g_sink_mix.load(std::memory_order_relaxed);
  if (want_mix && mix < 0) {
    char name[64] = {};
    neon::audio_channel_name(cfg, /*line_in=*/false, name, sizeof(name));
    mix = la.sink_create(name, kSampleRate, channels, kBlockFrames);
    g_sink_mix.store(mix, std::memory_order_relaxed);
  } else if (!want_mix && mix >= 0) {
    g_sink_mix.store(-1, std::memory_order_relaxed);
    la.sink_destroy(mix);
  }

  int line = g_sink_linein.load(std::memory_order_relaxed);
  if (want_line && line < 0) {
    char name[64] = {};
    neon::audio_channel_name(cfg, /*line_in=*/true, name, sizeof(name));
    line = la.sink_create(name, kSampleRate, channels, kBlockFrames);
    g_sink_linein.store(line, std::memory_order_relaxed);
  } else if (!want_line && line >= 0) {
    g_sink_linein.store(-1, std::memory_order_relaxed);
    la.sink_destroy(line);
  }
}

void audio_ctl_task(void*) {
  hal::ILinkAudio& la = ablink::link_audio();
  if (!la.available()) {
    ESP_LOGI(kTag, "Link Audio not in this build; publish/subscribe disabled");
    vTaskDelete(nullptr);
    return;
  }
  ablink::link_audio_start_pump();

  char subscribed_to[sizeof(neon::AudioConfig::la_sub_channel_id)] = {};
  uint8_t published_mono = 0xff;
  char peer_name[sizeof(neon::Config::device_name)] = {};
  uint32_t quantum = 0;
  int enabled = -1;

  // Diagnostics: previous counter values so the periodic log prints
  // deltas, and the last sub_state so transitions are logged as they
  // happen. All of this runs here on core 0 — a blocking ESP_LOG from the
  // render task would itself cause the underruns it reports.
  neon::AudioStatus prev{};
  uint8_t last_state = 0;
  uint32_t log_countdown = 0;

  // P4 UART CSV telemetry (docs/STUDIO_MODE_TEST_PLAN.md), off by default:
  // a header line whenever cfg.telemetry_uart_csv turns on, then one data
  // line per second while it stays on.
  bool telemetry_was_on = false;
  uint32_t telemetry_countdown = 0;

  for (;;) {
    const neon::Config& cfg = neon_config();
    // session.start() lives in link_svc, after the STA wait (up to 15 s).
    // This task is created the moment audio_service starts, so the first
    // ticks hit a null LinkAudio. Caching enable / peer-name across those
    // no-ops left enableLinkAudio(false) forever — Live saw no stream.
    if (!la.session_ready()) {
      enabled = -1;
      peer_name[0] = '\0';
      quantum = 0;
      published_mono = 0xff;
      subscribed_to[0] = '\0';
      vTaskDelay(pdMS_TO_TICKS(250));
      continue;
    }
    // setPeerName / enableLinkAudio reach into the Link session; calling
    // them every 250 ms with unchanged values is churn the session does
    // not need while it is trying to stream. Only cache after a call that
    // actually reached the instance.
    if (std::strcmp(peer_name, cfg.device_name) != 0) {
      std::snprintf(peer_name, sizeof(peer_name), "%s", cfg.device_name);
      la.set_peer_name(cfg.device_name);
    }
    if (cfg.quantum_beats != quantum) {
      quantum = cfg.quantum_beats;
      la.set_quantum(static_cast<double>(quantum));
    }
    const bool want_stream = cfg.audio.la_publish_mix != 0 ||
                             cfg.audio.la_publish_linein != 0 ||
                             cfg.audio.la_sub_channel_id[0] != '\0';
    if (static_cast<int>(want_stream) != enabled) {
      enabled = static_cast<int>(want_stream);
      la.set_enabled(want_stream);
      ESP_LOGI(kTag, "Link Audio %s", want_stream ? "enabled" : "disabled");
    }
    if (cfg.audio.la_publish_mono != published_mono) {
      // Channel count is fixed when a sink is created, so a mono/stereo
      // flip has to tear the sinks down and put them back.
      published_mono = cfg.audio.la_publish_mono;
      const int mix = g_sink_mix.exchange(-1, std::memory_order_relaxed);
      const int line = g_sink_linein.exchange(-1, std::memory_order_relaxed);
      if (mix >= 0) la.sink_destroy(mix);
      if (line >= 0) la.sink_destroy(line);
    }
    apply_publish_state(la, cfg);

    if (std::strcmp(subscribed_to, cfg.audio.la_sub_channel_id) != 0) {
      std::snprintf(subscribed_to, sizeof(subscribed_to), "%s",
                    cfg.audio.la_sub_channel_id);
      if (subscribed_to[0] == '\0') {
        la.unsubscribe();
        ESP_LOGI(kTag, "unsubscribed");
      } else if (!la.subscribe(subscribed_to)) {
        ESP_LOGW(kTag, "could not subscribe to \"%s\"", subscribed_to);
        subscribed_to[0] = '\0';
      }
    }

    // --- receive/publish diagnostics --------------------------------
    neon::AudioStatus st;
    audio_status_bus().read(st);

    // --- P4 UART CSV telemetry --------------------------------------
    const bool want_telemetry = cfg.telemetry_uart_csv != 0;
    if (want_telemetry && !telemetry_was_on) {
      char header[224];  // the header line itself is 200 bytes
      const size_t hn = neon::telemetry_csv_header(header, sizeof(header));
      if (hn != 0) {
        printf("TEL,%s\n", header);
      }
      telemetry_countdown = 0;
    }
    telemetry_was_on = want_telemetry;
    if (want_telemetry && telemetry_countdown-- == 0) {
      telemetry_countdown = 3;  // this loop runs every 250 ms; emit at 1 Hz
      const neon::ActiveNet net = netman::preference().active();
      const bool ap_up = netman::ap_is_up();
      const bool sta_up = net == neon::ActiveNet::kWifi;
      const char* mode = net == neon::ActiveNet::kEthernet ? "eth"
                         : ap_up && sta_up                  ? "apsta"
                         : ap_up                             ? "ap"
                         : sta_up                             ? "sta"
                                                              : "none";
      neon::TelemetrySample sample;
      sample.uptime_ms = static_cast<uint64_t>(esp_timer_get_time() / 1000);
      sample.mode = mode;
      sample.prio_set = st.priority_profile;
      sample.req_jitter_ms = st.req_jitter_ms;
      sample.eff_jitter_ms = st.eff_jitter_ms;
      sample.jit_fill_frames = st.fill_frames;
      sample.jit_underruns = st.jit_underruns;
      sample.jit_conceals = st.concealed;
      sample.jit_state = st.sub_state;
      sample.rx_dropped = st.rx_dropped;
      sample.rx_high_water = st.rx_high_water;
      sample.la_trim_ppm = st.trim_ppm;
      sample.i2s_write_failures = st.i2s_write_failures;
      sample.rssi = st.rssi;
      sample.heap_free_internal = st.heap_free_internal;
      sample.heap_free_psram = st.heap_free_psram;
      char line[192];
      const size_t ln = neon::telemetry_csv_line(sample, line, sizeof(line));
      if (ln != 0) {
        printf("TEL,%s\n", line);
      }
    }

    if (st.sub_state != last_state) {
      ESP_LOGI(kTag, "sub %s -> %s (fill %lu ms, rx_drop %lu, jit_drop %lu)",
               last_state == 0 ? "idle" : last_state == 1 ? "buffering"
                                                          : "playing",
               st.sub_state == 0 ? "idle" : st.sub_state == 1 ? "buffering"
                                                              : "playing",
               static_cast<unsigned long>(st.fill_ms),
               static_cast<unsigned long>(st.rx_dropped),
               static_cast<unsigned long>(st.sub_dropped));
      last_state = st.sub_state;
    }
    const bool streaming = st.sub_state != 0 || st.publishing != 0 ||
                           subscribed_to[0] != '\0';
    if (streaming && log_countdown-- == 0) {
      log_countdown = 20;  // every ~5 s while streaming
      ESP_LOGI(kTag,
               "la: state %u fill %lu ms trim %ld ppm | d5s rx_drop %lu "
               "jit_drop %lu rebuf %lu i2s_und %lu tx_drop %lu conceal %lu | "
               "clk %ld ppm",
               static_cast<unsigned>(st.sub_state),
               static_cast<unsigned long>(st.fill_ms),
               static_cast<long>(st.trim_ppm),
               static_cast<unsigned long>(st.rx_dropped - prev.rx_dropped),
               static_cast<unsigned long>(st.sub_dropped - prev.sub_dropped),
               static_cast<unsigned long>(st.jit_underruns -
                                          prev.jit_underruns),
               static_cast<unsigned long>(st.underruns - prev.underruns),
               static_cast<unsigned long>(st.tx_dropped - prev.tx_dropped),
               static_cast<unsigned long>(st.concealed - prev.concealed),
               static_cast<long>(st.clock_ppm));
      prev = st;
    } else if (!streaming) {
      log_countdown = 0;
      prev = st;
    }
    vTaskDelay(pdMS_TO_TICKS(250));
  }
}

}  // namespace

void neon_start_audio_service() {
  // Always start I2S. `audio.enabled` used to be restart-scoped, so a
  // saved "engine on" never reached the jack until a reboot — and a
  // boot with enabled=0 left LINE OUT dead for every other setting.
  // The render loop writes silence while the flag is off.
  if (neon_config().audio.enabled == 0) {
    ESP_LOGI(kTag, "audio starts muted (enabled=0); I2S is up so a save can unmute");
  }
  // Below the pulse task (MAX-2) and the CV mirror (MAX-3): the I2S DMA
  // gives this loop ~11 ms of slack, and the clock outputs give none.
  xTaskCreatePinnedToCore(audio_task, "audio", 8192, nullptr,
                          configMAX_PRIORITIES - 4, nullptr, 1);
  xTaskCreatePinnedToCore(audio_ctl_task, "audio_ctl", 4096, nullptr, 5,
                          nullptr, 0);
}

// Discovery for the editor: GET /api/audio/channels. snprintf returns the
// untruncated length, so the append is checked per channel: one that does
// not fit is dropped whole (with its partial write erased) and the
// document still closes — a crowded network shortens the picker instead of
// blanking it.
extern "C" int neon_audio_channels_json(char* buf, int cap) {
  hal::ILinkAudio& la = ablink::link_audio();
  hal::AudioChannelInfo channels[16];
  const size_t n = la.channels(channels, 16);
  int written = std::snprintf(buf, static_cast<size_t>(cap),
                              "{\"available\":%s,\"channels\":[",
                              la.available() ? "true" : "false");
  if (written < 0 || written + 2 >= cap) {
    return 0;
  }
  bool first = true;
  for (size_t i = 0; i < n; ++i) {
    // Room must remain for this entry AND the closing "]}".
    const int room = cap - written - 2;
    const int need = std::snprintf(
        buf + written, static_cast<size_t>(room),
        "%s{\"id\":\"%s\",\"name\":\"%s\",\"peer\":\"%s\",\"rate\":%u,"
        "\"channels\":%u,\"local\":%s}",
        first ? "" : ",", channels[i].id, channels[i].name, channels[i].peer,
        static_cast<unsigned>(channels[i].sample_rate),
        static_cast<unsigned>(channels[i].num_channels),
        channels[i].is_local ? "true" : "false");
    if (need < 0 || need >= room) {
      buf[written] = '\0';  // erase the truncated fragment
      break;
    }
    written += need;
    first = false;
  }
  written += std::snprintf(buf + written, static_cast<size_t>(cap - written),
                           "]}");
  return written;
}

#else  // !CONFIG_NEON_AUDIO

void neon_start_audio_service() {}

extern "C" int neon_audio_channels_json(char* buf, int cap) {
  return std::snprintf(buf, static_cast<size_t>(cap),
                       "{\"available\":false,\"channels\":[]}");
}

#endif  // CONFIG_NEON_AUDIO
