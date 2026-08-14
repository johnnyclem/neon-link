#include "config_store_t41.h"

#include <Arduino.h>
#include <EEPROM.h>

namespace cfgstore {
namespace {
// The blob comfortably fits the 4 KB emulation today; a static_assert
// can't see config_blob_size() (runtime), so load/save bounds-check.
uint8_t g_buf[4096];
}  // namespace

bool load(neon::Config* cfg) {
  const size_t len = neon::config_blob_size();
  if (len > sizeof(g_buf) || len > static_cast<size_t>(EEPROM.length())) {
    return false;
  }
  for (size_t i = 0; i < len; ++i) {
    g_buf[i] = EEPROM.read(static_cast<int>(i));
  }
  return neon::config_decode(g_buf, len, cfg);
}

void save(const neon::Config& cfg) {
  const size_t len = neon::config_encode(cfg, g_buf, sizeof(g_buf));
  if (len == 0 || len > static_cast<size_t>(EEPROM.length())) {
    return;
  }
  for (size_t i = 0; i < len; ++i) {
    EEPROM.update(static_cast<int>(i), g_buf[i]);
  }
}

}  // namespace cfgstore
