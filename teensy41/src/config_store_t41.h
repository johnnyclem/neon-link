#pragma once

#include "neon/config/model.hpp"

// Config persistence in the Teensy 4.1's emulated EEPROM (4 KB), using
// the same encoded blob (magic + version + CRC) the ESP32 target stores
// in NVS — config_decode rejects anything stale or corrupt and the
// caller keeps defaults.
namespace cfgstore {

// Fills *cfg from EEPROM; returns false (leaving defaults) on any
// mismatch.
bool load(neon::Config* cfg);

// Encode + write (EEPROM.update per byte, so unchanged bytes cost no
// wear). Call debounced — flash sectors behind the EEPROM emulation
// have a finite erase life.
void save(const neon::Config& cfg);

}  // namespace cfgstore
