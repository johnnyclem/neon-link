#pragma once

#include <cstdint>

// Standalone momentary buttons (to GND, internal pullups) from the
// board header's kButtonPins table — the same debounced click / 600 ms
// long-press state machine the encoder switches use. Compiled only on
// boards that have any (Pod, patch.init()).
namespace btn {

enum class Event : uint8_t { kNone, kClick, kLongPress };

void init();
Event poll(int index, uint32_t now_ms);

// Debounced level (true = held), for hold-style modifiers.
bool held(int index);

}  // namespace btn
