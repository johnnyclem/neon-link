#pragma once

#include "daisy_seed.h"

// The one board object (SDRAM, QSPI, codec, system clocks), defined in
// main.cpp and initialized before any service init runs.
extern daisy::DaisySeed g_seed;
