#pragma once

#include "sdkconfig.h"

// CST816 capacitive touch controller (I2C @ 0x15). Used by the MaTouch
// 1.28" round board. Polled single-touch: enough for tapping the gear and
// the settings rows. Coordinates are returned in panel pixels (0..239),
// already X-mirrored to match the GC9A01 orientation.

namespace halesp {

// Reset the controller and bring up the shared I2C bus on sda/scl. rst is
// pulsed low→high; pass -1 to skip. Returns true if the chip ACKs.
bool cst816_init(int sda_gpio, int scl_gpio, int rst_gpio);

// Latest sample. Returns true while a finger is down and fills *x/*y.
bool cst816_poll(int* x, int* y);

}  // namespace halesp
