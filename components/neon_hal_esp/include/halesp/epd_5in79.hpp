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
// (including the fast-LUT load) and discards the partial-refresh base
// (reset clears the controller).
void epd5in79_awaken();
void epd5in79_clear();
// Packed row-major 1-bit buffer, 99 bytes/row, 272 rows, 1 = white.
// fast=false → full GC refresh (0xF7): seconds, flashes the glass,
// clears ghosting. fast=true → fast refresh (0xC7): ~1 s, no flash,
// repaints the whole glass; needs the LUT loaded with a temperature
// operand at init (0x1A/0x22{0x91}, the step the vendor demo hides in
// Init_Fast — see SOLAROS_PORTS_HANDOFF.md §2). Both seed BOTH RAM
// planes so banded partials can follow.
void epd5in79_display(const uint8_t* frame, bool fast);
// Partial refresh (0xFF) of a horizontal band of buffer rows
// [row0, row0+rows): sub-second, no flash, streams only the band.
// Invariant maintained: after every display/partial call, old RAM ==
// new RAM == glass everywhere, so an update can never repaint stale
// content outside the band (the band is written to both planes again
// after the update — RAM ping-pongs on 0xFF). Accumulates ghosting;
// callers interleave full refreshes (see neon::EpdRefreshPlanner).
// Falls back to a full refresh if no base has been seeded since the
// last init/awaken.
void epd5in79_display_partial_rows(const uint8_t* frame, int row0, int rows);
// Whole-frame band, kept for callers that don't track dirty rows.
void epd5in79_display_partial(const uint8_t* frame);
void epd5in79_sleep();

}  // namespace halesp
