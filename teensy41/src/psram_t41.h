#pragma once

#include <cstddef>
#include <cstdint>

// The 16 MB of PSRAM soldered to the Teensy 4.1's underside pads
// (2x 8 MB chips; Teensyduino maps them contiguously at 0x70000000 as
// EXTMEM). This target reserves it for the buffers that dwarf on-chip
// RAM — Link Audio jitter buffers, config JSON scratch, screenshots —
// via a simple bump arena over extmem_malloc.
namespace psram {

struct Report {
  uint32_t megabytes = 0;   // what the startup code detected (expect 16)
  bool test_ok = false;     // address-pattern spot check across the range
  uint32_t fail_addr = 0;   // first failing address when !test_ok
};

// Detect + spot-check. Call once from setup(), before allocations.
Report init(uint32_t expect_mb = 16);

// Arena allocation (8-byte aligned, never freed). Returns nullptr when
// PSRAM is absent or exhausted.
void* alloc(size_t bytes);

size_t used_bytes();

}  // namespace psram
