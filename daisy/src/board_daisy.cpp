#include "board_daisy.h"

#include "board_pins_daisy.h"

#if defined(NEON_BOARD_PATCH_INIT)

daisy::patch_sm::DaisyPatchSM g_patch;

void board_init() {
  g_patch.Init();  // SDRAM, QSPI, PCM3060, ADCs, CV DAC, clocks
}

daisy::QSPIHandle& board_qspi() { return g_patch.qspi; }

void board_audio_start(daisy::AudioHandle::AudioCallback cb,
                       size_t block_frames) {
  g_patch.SetAudioBlockSize(block_frames);
  g_patch.SetAudioSampleRate(daisy::SaiHandle::Config::SampleRate::SAI_48KHZ);
  g_patch.StartAudio(cb);
}

void board_tempo_cv_write(uint16_t ratio_q16) {
  g_patch.WriteCvOut(daisy::patch_sm::CV_OUT_2,
                     5.0f * static_cast<float>(ratio_q16) / 65535.0f);
}

void board_set_led(bool on) { g_patch.SetLed(on); }

#else  // Seed and Pod (a Pod is a Seed with a control panel)

daisy::DaisySeed g_seed;

namespace {
bool g_dac_ready = false;
}  // namespace

void board_init() {
  g_seed.Init();  // SDRAM, QSPI (memory-mapped), codec, clocks

  daisy::DacHandle::Config cfg;
  cfg.target_samplerate = 0;  // unused in polling mode
  cfg.chn = kTempoCvDacChannel;
  cfg.mode = daisy::DacHandle::Mode::POLLING;
  cfg.bitdepth = daisy::DacHandle::BitDepth::BITS_12;
  cfg.buff_state = daisy::DacHandle::BufferState::ENABLED;
  g_dac_ready = g_seed.dac.Init(cfg) == daisy::DacHandle::Result::OK;
}

daisy::QSPIHandle& board_qspi() { return g_seed.qspi; }

void board_audio_start(daisy::AudioHandle::AudioCallback cb,
                       size_t block_frames) {
  g_seed.SetAudioBlockSize(block_frames);
  g_seed.SetAudioSampleRate(daisy::SaiHandle::Config::SampleRate::SAI_48KHZ);
  g_seed.StartAudio(cb);
}

void board_tempo_cv_write(uint16_t ratio_q16) {
  if (!g_dac_ready) {
    return;
  }
  g_seed.dac.WriteValue(kTempoCvDacChannel,
                        static_cast<uint16_t>(ratio_q16 >> 4));
}

void board_set_led(bool on) { g_seed.SetLed(on); }

#endif
