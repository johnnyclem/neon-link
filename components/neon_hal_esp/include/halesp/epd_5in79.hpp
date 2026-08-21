#pragma once

#include <cstddef>
#include <cstdint>

namespace halesp {

// Waveshare 5.79" e-Paper (792×272, dual SSD1683). Init sequence and
// dual-IC RAM split follow the official MIT demo
// (waveshareteam/e-Paper Arduino/epd5in79).

bool epd5in79_init(int sck, int mosi, int cs, int dc, int rst, int busy,
                   int pwr);
// After Sleep(), call before the next display. Re-runs reset + windows
// and discards the partial-refresh base (reset clears the controller).
void epd5in79_awaken();
void epd5in79_clear();
// Packed row-major 1-bit buffer, 99 bytes/row, 272 rows, 1 = white.
// Full refresh (0xF7): seconds, flashes the glass, clears ghosting.
// Also seeds the old RAM so partial refreshes can follow.
void epd5in79_display(const uint8_t* frame, bool fast);
// Partial refresh (0xFF): sub-second, no flash, diffs the frame
// against the last displayed one. Accumulates ghosting — callers
// should interleave full refreshes (see neon::EpdRefreshPlanner).
// Falls back to a full refresh if no base has been seeded since the
// last init/awaken.
void epd5in79_display_partial(const uint8_t* frame);
void epd5in79_sleep();

}  // namespace halesp
