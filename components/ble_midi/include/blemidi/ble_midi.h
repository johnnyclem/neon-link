#pragma once

#include <cstddef>
#include <cstdint>

namespace blemidi {

// Raw BLE-MIDI packets (one GATT write each) are handed to this callback
// from the NimBLE host task — copy out and return quickly.
using PacketHandler = void (*)(const uint8_t* data, size_t len);

// Bring up NimBLE, register the MIDI service (03B8...C700 / 7772...6BF3),
// and advertise as "NEON LINK". Returns false when BLE is compiled out
// (CONFIG_BT_NIMBLE_ENABLED unset) or init fails.
bool start(PacketHandler handler);

// Kill switch: stop advertising and fully deinitialize the stack.
void stop();

bool connected();

}  // namespace blemidi
