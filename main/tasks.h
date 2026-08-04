#pragma once

// Core 0: networking / Link / BLE / web / OLED (application side).
void neon_start_core0_tasks();

// Core 1: real-time pulse engine. Nothing else runs at this priority.
void neon_start_core1_tasks();
