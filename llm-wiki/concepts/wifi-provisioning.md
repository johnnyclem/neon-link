# Concept: Wi-Fi provisioning, STA→AP fallback, and two bugs fixed

Files: `main/wifi.cpp` (STA), `components/net_manager/src/net_manager.cpp`
(AP + netifs), `main/link_service.cpp` (orchestration), on-device join UI in
`main/matouch_service.cpp` (encoder character picker).

## Flow
- Boot: `neon_wifi_start()` creates the default STA netif (guarded) and tries
  stored credentials, `wifi_retries` each. On repeated failure the STA
  **retries forever** (no give-up) — `on_wifi_event` → `advance_after_failure`.
- If no IP after `kWifiWaitMs` and `ap_policy == kFallback`,
  `link_service` calls `neon_wifi_hold_station()` + `start_ap_from_config()`
  → `netman::ap_start()` brings up the setup AP at 192.168.4.1.
- `neon_config_load()` reads NVS; **NVS credentials override the menuconfig
  `CONFIG_NEON_WIFI_*` defaults.** So to use a baked default you must
  `erase-flash` the `nvs` partition first (a normal reflash keeps it).

## Bug 1 — a wrong password was persisted before it ever connected
`neon_config_apply()` (`config_store.cpp`) persists to NVS **immediately**
when the network identity changes (the G6 "friend Save must hit flash" path).
So the on-device join wrote a half-typed/wrong password to flash the instant
connect was attempted → survived reboots → the STA retried it forever.

**Fix (try-then-persist):**
- Added `neon_config_apply_ram()` — sanitize + publish to RAM only, never
  persist, never mark save-pending (so a later debounced flush can't leak it).
- `matouch` `wifi_connect()` applies the candidate with `apply_ram` and
  refuses a secured join with a `< 8`-char password.
- `wifi_tick()` calls `neon_config_save()` only once an IP actually arrives —
  the sole path that writes a joined credential.

## Bug 2 — STA-fail → setup-AP → reboot loop (the DoA one)
Symptom: `assert failed: netif_add ... (netif already added)` ~16 s after
every boot when the stored password was wrong; looks like a spontaneous
reboot unrelated to whatever the user was touching.

Root cause: `esp_netif_create_default_wifi_sta()` (used by `main/wifi.cpp`)
calls `esp_wifi_set_default_wifi_sta_handlers()`, which is the **shared**
`set_default_wifi_handlers()` in `esp_wifi/src/wifi_default.c` — it registers
`WIFI_EVENT_AP_START → wifi_default_action_ap_start` **too**, not just STA
handlers. So IDF already netif_adds the AP interface on AP_START, and
`net_manager::on_ap_event` added it a **second** time → lwIP assert → reboot.
It only bit **native** builds; the Hosted C6 boards (P4LCD/Tab5, via
`esp_wifi_remote`) don't use that shared STA path, so no default AP handler
exists there — which is why the P4 never crashed and the S3 dial did.

**Fix:** `constexpr bool kApSelfStartsNetif` (net_manager) — true only on
Hosted (P4LCD/Tab5). On native, `on_ap_event` records status and lets IDF's
handler do the single `netif_add`; on Hosted we still add it ourselves. A
wrong password now leaves the dial running (offline, setup AP reachable)
instead of crash-looping.

## Gotchas worth remembering
- A bad Wi-Fi credential could **brick** the unit pre-fix — critical for a
  shipping product. Always test the STA-fail path, not just the happy path.
- `erase-flash` is the reliable recovery for a poisoned NVS credential.
- See [lessons](../lessons/debugging-lessons.md) for how long this hid behind
  wrong theories (it presented as "encoder/MIDI crashes").
