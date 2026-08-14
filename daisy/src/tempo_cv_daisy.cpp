#include "tempo_cv_daisy.h"

#include "daisy_seed.h"

#include "seed_hw_daisy.h"

namespace tempocv {
namespace {
bool g_ready = false;
}  // namespace

void init() {
  daisy::DacHandle::Config cfg;
  cfg.target_samplerate = 0;  // unused in polling mode
  cfg.chn = daisy::DacHandle::Channel::ONE;
  cfg.mode = daisy::DacHandle::Mode::POLLING;
  cfg.bitdepth = daisy::DacHandle::BitDepth::BITS_12;
  cfg.buff_state = daisy::DacHandle::BufferState::ENABLED;
  g_ready = g_seed.dac.Init(cfg) == daisy::DacHandle::Result::OK;
}

void write_ratio_q16(uint16_t ratio) {
  if (!g_ready) {
    return;
  }
  g_seed.dac.WriteValue(daisy::DacHandle::Channel::ONE,
                        static_cast<uint16_t>(ratio >> 4));
}

}  // namespace tempocv
