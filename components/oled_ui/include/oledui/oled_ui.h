#pragma once

// Probe the Grove panel and wipe GDDRAM. Call from app_main after I2C
// is up and before any other I2C talker (CV mirror, expander) starts —
// otherwise a cold-boot scan can starve the idle task and the WDT
// resets us into the blank two-pixel state.
void oledui_bringup();

// Starts the local UI: encoder navigation, status LEDs, menu. Runs as a
// low-priority core-0 task at ~10 fps. Requires neon_config_load() and
// oledui_bringup() to have run.
void oledui_start();
