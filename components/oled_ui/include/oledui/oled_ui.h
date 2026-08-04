#pragma once

// Starts the local UI: SSD1306 OLED over I2C, encoder navigation, status
// LEDs. Runs as a low-priority core-0 task at ~15 fps. Requires
// neon_config_load() to have run.
void oledui_start();
