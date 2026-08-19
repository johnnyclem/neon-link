#pragma once

#include <cstddef>
#include <cstdint>

namespace halesp {

// Waveshare 5.79" e-Paper (792×272, dual SSD1683). Init sequence and
// dual-IC RAM split follow the official MIT demo
// (waveshareteam/e-Paper Arduino/epd5in79).

bool epd5in79_init(int sck, int mosi, int cs, int dc, int rst, int busy,
                   int pwr);
// After Sleep(), call before the next display. Re-runs reset + windows.
void epd5in79_awaken();
void epd5in79_clear();
// Packed row-major 1-bit buffer, 99 bytes/row, 272 rows, 1 = white.
void epd5in79_display(const uint8_t* frame, bool fast);
void epd5in79_sleep();

}  // namespace halesp
