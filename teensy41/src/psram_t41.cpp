#include "psram_t41.h"

#include <Arduino.h>

// Teensyduino core: PSRAM size in MB detected at startup (0 when no
// chips respond), and the EXTMEM heap over the same region.
extern "C" uint8_t external_psram_size;
extern "C" void* extmem_malloc(size_t);

namespace psram {
namespace {

Report g_report;
size_t g_used = 0;

// Write an address-derived pattern at a stride, then verify. Touches
// every megabyte without the multi-second cost of a full 16 MB sweep.
bool spot_check(uint32_t bytes, uint32_t* fail_addr) {
  constexpr uint32_t kStride = 64 * 1024;
  volatile uint32_t* const base = reinterpret_cast<uint32_t*>(0x70000000);
  for (uint32_t off = 0; off < bytes; off += kStride) {
    base[off / 4] = off ^ 0xA5A55A5Au;
  }
  for (uint32_t off = 0; off < bytes; off += kStride) {
    if (base[off / 4] != (off ^ 0xA5A55A5Au)) {
      *fail_addr = 0x70000000u + off;
      return false;
    }
  }
  return true;
}

}  // namespace

Report init(uint32_t expect_mb) {
  g_report.megabytes = external_psram_size;
  if (g_report.megabytes == 0) {
    g_report.test_ok = false;
    g_report.fail_addr = 0;
    return g_report;
  }
  const uint32_t mb =
      g_report.megabytes < expect_mb ? g_report.megabytes : expect_mb;
  g_report.test_ok = spot_check(mb * 1024u * 1024u, &g_report.fail_addr);
  return g_report;
}

void* alloc(size_t bytes) {
  if (g_report.megabytes == 0) {
    return nullptr;
  }
  void* p = extmem_malloc(bytes);
  if (p != nullptr) {
    g_used += bytes;
  }
  return p;
}

size_t used_bytes() { return g_used; }

}  // namespace psram
