#pragma once

// Starts the embedded web editor: serves the single-page UI at / and the
// REST API at /api/config (GET/PUT) and /api/status (GET). Binds all
// interfaces — reachable via neon-link.local, the STA address, or
// 192.168.4.1 in setup-AP mode.
void webui_start();
