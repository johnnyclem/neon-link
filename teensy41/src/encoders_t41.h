#pragma once

#include <cstdint>

// The two front-panel encoders, decoded in pin-change interrupts through
// the same neon::QuadDecoder the host tests exercise (x4, one detent per
// gray cycle). Index 0 = ENC1 (menu), index 1 = ENC2 (tempo/transport).
namespace enc {

enum class ButtonEvent : uint8_t {
  kNone = 0,
  kClick,      // released before the long-press threshold
  kLongPress,  // fired once while still held
};

void init();

// Detents accumulated since the last call (ISR-safe drain).
int take_detents(int index);

// Debounced push-switch events; call every loop iteration.
ButtonEvent poll_button(int index, uint32_t now_ms);

}  // namespace enc
