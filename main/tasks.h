#pragma once

#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "sdkconfig.h"

// Pulse/audio tasks pin to core 1 on dual-core chips. ESP32-C3 is unicore
// — pinning to 1 fails and the task never runs.
inline constexpr BaseType_t kNeonCoreApp = 0;
#if CONFIG_FREERTOS_UNICORE
inline constexpr BaseType_t kNeonCoreRt = 0;
#else
inline constexpr BaseType_t kNeonCoreRt = 1;
#endif

// Core 0: networking / Link / BLE / web / OLED (application side).
void neon_start_core0_tasks();

// Core 0: WiFi + Ableton Link session + timeline snapshot publisher.
void neon_start_link_service();

// Core 0: BLE MIDI receive/routing + TRS MIDI out with Link-derived clock.
void neon_start_midi_service();

// Core 1: real-time pulse engine. Nothing else runs at this priority.
void neon_start_core1_tasks();

// Core 0: single inverted user LED (XIAO link-sync). No-op elsewhere.
void neon_start_status_led_service();

// Core 0: Waveshare 5.79" e-paper status. No-op on other boards.
void neon_start_epd_service();

// Core 0: CrowPanel Advance 5.0" RGB LCD status. No-op on other boards.
void neon_start_lcd_service();

// Core 0: ESP32-C3 stamp 72×40 OLED. No-op on other boards.
void neon_start_c3oled_service();

// Core 0: MaTouch 1.28" round GC9A01 + encoder. No-op on other boards.
void neon_start_matouch_service();

// Core 0: Waveshare 4.2" ST7305 reflective LCD. No-op on other boards.
void neon_start_rlcd_service();

// Core 0: 1 Hz TEL CSV on the console UART (link-sync). No-op elsewhere.
void neon_start_telemetry_service();

// Core 0: OSC over UDP — /neon/* control in, tempo/playing/beat/peers
// out. Idles until cfg.osc_enabled is set (docs/OSC.md).
void neon_start_osc_service();

// Core 1: the I2S audio render loop, plus its core-0 control task.
// A no-op unless CONFIG_NEON_AUDIO is set and audio is enabled in the
// stored configuration.
void neon_start_audio_service();

// Link Audio channel discovery for the editor. Always defined; reports
// available:false when the build has no Link Audio behind it.
extern "C" int neon_audio_channels_json(char* buf, int cap);

// T3 / G3: busy-wait the I2S render task for `ms` before the next
// write_block so the DMA ring starves. No-op if audio is not running.
extern "C" void neon_audio_request_stall_ms(uint32_t ms);
