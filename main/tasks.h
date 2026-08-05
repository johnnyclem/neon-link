#pragma once

// Core 0: networking / Link / BLE / web / OLED (application side).
void neon_start_core0_tasks();

// Core 0: WiFi + Ableton Link session + timeline snapshot publisher.
void neon_start_link_service();

// Core 0: BLE MIDI receive/routing + TRS MIDI out with Link-derived clock.
void neon_start_midi_service();

// Core 1: real-time pulse engine. Nothing else runs at this priority.
void neon_start_core1_tasks();
