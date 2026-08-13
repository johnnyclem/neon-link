#pragma once

// Core 0: networking / Link / BLE / web / OLED (application side).
void neon_start_core0_tasks();

// Core 0: WiFi + Ableton Link session + timeline snapshot publisher.
void neon_start_link_service();

// Core 0: BLE MIDI receive/routing + TRS MIDI out with Link-derived clock.
void neon_start_midi_service();

// Core 1: real-time pulse engine. Nothing else runs at this priority.
void neon_start_core1_tasks();

// Core 1: the I2S audio render loop, plus its core-0 control task.
// A no-op unless CONFIG_NEON_AUDIO is set and audio is enabled in the
// stored configuration.
void neon_start_audio_service();

// Link Audio channel discovery for the editor. Always defined; reports
// available:false when the build has no Link Audio behind it.
extern "C" int neon_audio_channels_json(char* buf, int cap);
