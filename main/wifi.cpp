#include "wifi.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "sdkconfig.h"
#include "app_state/config_store.h"
#include "esp_log.h"
#include "netman/net_manager.h"

#if CONFIG_ESP_WIFI_ENABLED || CONFIG_ESP_WIFI_REMOTE_ENABLED
#include "esp_event.h"
#include "esp_netif.h"
#ifndef CONFIG_WIFI_RMT_CACHE_TX_BUFFER_NUM
#define CONFIG_WIFI_RMT_CACHE_TX_BUFFER_NUM 32
#endif
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#define NEON_HAVE_WIFI 1
#else
#define NEON_HAVE_WIFI 0
#endif

#if NEON_HAVE_WIFI

namespace {

const char* kTag = "wifi";
constexpr EventBits_t kGotIpBit = BIT0;

EventGroupHandle_t g_events = nullptr;
bool g_handlers_registered = false;
bool g_sta_started = false;
bool g_sta_enabled = false;       // false after hold: do not reconnect
bool g_connect_requested = false; // in-flight esp_wifi_connect()
uint8_t g_last_disconnect_reason = 0;

// Which stored network we are currently trying, and how many attempts it
// has had. The module walks the list in order, giving each network
// `wifi_retries` attempts before moving on — the behavior users expect
// from a box that follows them between studio, rehearsal room, and stage.
int g_slot = 0;
uint8_t g_attempts = 0;

const neon::WifiNetwork* slot_at(int i) {
  if (i < 0 || i >= neon::kWifiSlots) {
    return nullptr;
  }
  const neon::WifiNetwork& n = neon_config().wifi[i];
  return n.ssid[0] != '\0' ? &n : nullptr;
}

int configured_count() {
  int n = 0;
  for (int i = 0; i < neon::kWifiSlots; ++i) {
    if (slot_at(i) != nullptr) {
      ++n;
    }
  }
  return n;
}

// Next slot with an SSID, wrapping. Returns -1 when the list is empty.
int next_slot(int from) {
  for (int step = 1; step <= neon::kWifiSlots; ++step) {
    const int i = (from + step) % neon::kWifiSlots;
    if (slot_at(i) != nullptr) {
      return i;
    }
  }
  return slot_at(from) != nullptr ? from : -1;
}

// Stored config wins; menuconfig is the fallback for development builds.
const char* effective_ssid() {
  const neon::WifiNetwork* n = slot_at(g_slot);
  if (n != nullptr) {
    return n->ssid;
  }
  return configured_count() == 0 ? CONFIG_NEON_WIFI_SSID : "";
}

const char* effective_pass() {
  const neon::WifiNetwork* n = slot_at(g_slot);
  if (n != nullptr) {
    return n->pass;
  }
  return configured_count() == 0 ? CONFIG_NEON_WIFI_PASSWORD : "";
}


const char* reason_name(uint8_t r) {
  switch (r) {
    case WIFI_REASON_AUTH_EXPIRE:
      return "auth_expire";
    case WIFI_REASON_AUTH_FAIL:
      return "auth_fail";
    case WIFI_REASON_ASSOC_FAIL:
      return "assoc_fail";
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
      return "handshake_timeout";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
      return "4way_handshake_timeout(wrong_password?)";
    case WIFI_REASON_NO_AP_FOUND:
      return "no_ap_found";
    case WIFI_REASON_BEACON_TIMEOUT:
      return "beacon_timeout";
    case WIFI_REASON_CONNECTION_FAIL:
      return "connection_fail";
    case WIFI_REASON_AUTH_LEAVE:
      return "auth_leave";
    default:
      return "other";
  }
}

void fill_sta_config(wifi_config_t* cfg) {
  std::memset(cfg, 0, sizeof(*cfg));
  std::strncpy(reinterpret_cast<char*>(cfg->sta.ssid), effective_ssid(),
               sizeof(cfg->sta.ssid) - 1);
  std::strncpy(reinterpret_cast<char*>(cfg->sta.password), effective_pass(),
               sizeof(cfg->sta.password) - 1);
  // Accept open through WPA2/WPA3-transition. Do not force WPA2-only.
  cfg->sta.threshold.authmode = WIFI_AUTH_OPEN;
  cfg->sta.pmf_cfg.capable = true;
  cfg->sta.pmf_cfg.required = false;
  cfg->sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
  // All-channel scan either way; a hidden network is only reachable
  // because the SSID is configured explicitly, not from a probe response.
  cfg->sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
  cfg->sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
}

// Kick off a connect. A second call while one is in flight returns
// ESP_ERR_WIFI_CONN — ignore that; do not treat it as a new attempt.
void request_connect() {
  if (!g_sta_enabled) {
    return;
  }
  g_connect_requested = true;
  const esp_err_t err = esp_wifi_connect();
  if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) {
    g_connect_requested = false;
    ESP_LOGW(kTag, "esp_wifi_connect: %s", esp_err_to_name(err));
  }
}

// Reconfigure the STA for the current slot and kick off a connect.
void connect_current_slot() {
  wifi_config_t cfg = {};
  fill_sta_config(&cfg);
  if (esp_wifi_set_config(WIFI_IF_STA, &cfg) != ESP_OK) {
    ESP_LOGW(kTag, "set_config failed");
    return;
  }
  request_connect();
}

// Count this failure; roll to the next stored network once the current one
// has used up its attempts.
void advance_after_failure() {
  const uint8_t retries =
      neon_config().wifi_retries != 0 ? neon_config().wifi_retries : 1;
  if (g_attempts < 255) {
    ++g_attempts;
  }
  if (g_attempts < retries || configured_count() < 2) {
    return;
  }
  const int next = next_slot(g_slot);
  if (next >= 0 && next != g_slot) {
    g_slot = next;
    g_attempts = 0;
    ESP_LOGI(kTag, "trying stored network %d: \"%s\"", g_slot,
             effective_ssid());
  } else {
    g_attempts = 0;
  }
}

void on_wifi_event(void*, esp_event_base_t base, int32_t id, void* event_data) {
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
    request_connect();
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    auto* disc = static_cast<wifi_event_sta_disconnected_t*>(event_data);
    g_last_disconnect_reason = disc != nullptr ? disc->reason : 0;
    g_connect_requested = false;
    if (g_events != nullptr) {
      xEventGroupClearBits(g_events, kGotIpBit);
    }
    netman::preference().wifi_ip(false);
    if (!g_sta_enabled) {
      ESP_LOGI(kTag, "disconnected reason=%u (%s) — STA held, not retrying",
               static_cast<unsigned>(g_last_disconnect_reason),
               reason_name(g_last_disconnect_reason));
      return;
    }
    ESP_LOGW(kTag, "disconnected reason=%u (%s) ssid=\"%s\" — retrying",
             static_cast<unsigned>(g_last_disconnect_reason),
             reason_name(g_last_disconnect_reason), effective_ssid());
    const int prev_slot = g_slot;
    advance_after_failure();
    // Do not block the event loop: a delay here stalls AP-start and IP
    // events, and a scanning STA makes the setup AP vanish from phones.
    if (g_slot != prev_slot) {
      connect_current_slot();
    } else {
      request_connect();
    }
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    auto* event = static_cast<ip_event_got_ip_t*>(event_data);
    g_last_disconnect_reason = 0;
    g_attempts = 0;  // this network works; keep it as the preferred slot
    ESP_LOGI(kTag, "got IP " IPSTR " — http://%s.local/ or http://" IPSTR "/",
             IP2STR(&event->ip_info.ip), neon_config().device_name,
             IP2STR(&event->ip_info.ip));
    netman::preference().wifi_ip(true);
    if (g_events != nullptr) {
      xEventGroupSetBits(g_events, kGotIpBit);
    }
  }
}

void ensure_events() {
  if (g_events == nullptr) {
    g_events = xEventGroupCreate();
  }
}

void ensure_handlers() {
  if (g_handlers_registered) {
    return;
  }
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                             &on_wifi_event, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                             &on_wifi_event, nullptr));
  g_handlers_registered = true;
}

bool ensure_wifi_driver() { return netman::wifi_driver_init(); }

// Point g_slot at the first configured network (called before the first
// connect and whenever the stored list is edited).
void reset_slot_cursor() {
  for (int i = 0; i < neon::kWifiSlots; ++i) {
    if (slot_at(i) != nullptr) {
      g_slot = i;
      g_attempts = 0;
      return;
    }
  }
  g_slot = 0;
  g_attempts = 0;
}

}  // namespace

bool neon_wifi_has_credentials() {
  if (neon_config().ap_policy == neon::ApPolicy::kAlways) {
    return false;  // self-hosted network only; never join anything
  }
  if (configured_count() != 0) {
    return true;
  }
  return CONFIG_NEON_WIFI_SSID[0] != '\0';
}

void neon_wifi_start() {
  if (!neon_wifi_has_credentials()) {
    ESP_LOGW(kTag, "no WiFi credentials configured; skipping WiFi");
    return;
  }
  reset_slot_cursor();
  ensure_events();
  netman::init_common();
  if (esp_netif_get_handle_from_ifkey("WIFI_STA_DEF") == nullptr) {
    esp_netif_create_default_wifi_sta();
  }
  if (!ensure_wifi_driver()) {
    ESP_LOGE(kTag, "esp_wifi_init failed");
    return;
  }
  ensure_handlers();

  wifi_config_t cfg = {};
  fill_sta_config(&cfg);

  // Setup AP is AP-only (APSTA hops the beacon channel, and the C3 Super
  // Mini never associates while the AP is still up). Drop it so STA can
  // join; the editor already got its HTTP reply.
  if (netman::ap_is_up()) {
    netman::ap_stop();
  }
  // Hold off STA_START → connect until the SSID is programmed.
  g_sta_enabled = false;
  const wifi_mode_t mode = WIFI_MODE_STA;
  ESP_ERROR_CHECK(esp_wifi_set_mode(mode));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
  g_sta_enabled = true;
  (void)esp_wifi_set_ps(WIFI_PS_NONE);
  const esp_err_t start_err = esp_wifi_start();
  if (start_err != ESP_OK && start_err != ESP_ERR_WIFI_NOT_INIT &&
      start_err != ESP_ERR_INVALID_STATE) {
    ESP_LOGW(kTag, "esp_wifi_start: %s", esp_err_to_name(start_err));
  }
  if (start_err == ESP_OK || start_err == ESP_ERR_INVALID_STATE) {
    netman::wifi_after_start();
  }
  // Fresh start delivers WIFI_EVENT_STA_START, which calls request_connect.
  // Already-running driver (INVALID_STATE) will not, so connect here.
  if (start_err == ESP_ERR_INVALID_STATE) {
    request_connect();
  }
  g_sta_started = true;
  ESP_LOGI(kTag,
           "connecting to \"%s\" (pass_len=%u, %d stored, %u tries each) — "
           "ESP32 is 2.4 GHz only",
           effective_ssid(),
           static_cast<unsigned>(std::strlen(effective_pass())),
           configured_count(),
           static_cast<unsigned>(neon_config().wifi_retries));
}

void neon_wifi_hold_station() {
  if (!g_sta_enabled && !g_sta_started) {
    return;
  }
  g_sta_enabled = false;
  g_connect_requested = false;
  if (g_events != nullptr) {
    xEventGroupClearBits(g_events, kGotIpBit);
  }
  netman::preference().wifi_ip(false);
  (void)esp_wifi_disconnect();
  ESP_LOGI(kTag, "STA held — setup AP can beacon without STA scans");
}

extern "C" void neon_wifi_apply_credentials(void) {
  netman::preference().wifi_configured(neon_wifi_has_credentials());
  if (!neon_wifi_has_credentials()) {
    ESP_LOGW(kTag, "apply: no credentials");
    return;
  }

  if (netman::ap_is_up()) {
    netman::ap_stop();
  }
  if (!g_sta_started) {
    neon_wifi_start();
    return;
  }

  reset_slot_cursor();
  ensure_events();
  ensure_handlers();
  g_sta_enabled = false;
  (void)esp_wifi_set_mode(WIFI_MODE_STA);
  (void)esp_wifi_set_ps(WIFI_PS_NONE);
  g_connect_requested = false;
  (void)esp_wifi_disconnect();
  g_sta_enabled = true;
  connect_current_slot();
  ESP_LOGI(kTag, "reconnecting to \"%s\" (pass_len=%u)", effective_ssid(),
           static_cast<unsigned>(std::strlen(effective_pass())));
}

static void apply_later_task(void*) {
  vTaskDelay(pdMS_TO_TICKS(400));
  neon_wifi_apply_credentials();
  vTaskDelete(nullptr);
}

extern "C" void neon_wifi_apply_credentials_later(void) {
  if (xTaskCreate(apply_later_task, "wifi_apply", 4096, nullptr, 5, nullptr) !=
      pdPASS) {
    neon_wifi_apply_credentials();
  }
}

bool neon_wifi_sta_got_ip() {
#if NEON_HAVE_WIFI
  return g_events != nullptr &&
         (xEventGroupGetBits(g_events) & kGotIpBit) != 0;
#else
  return false;
#endif
}

bool neon_wifi_wait_ip(uint32_t timeout_ms) {
  if (g_events == nullptr) {
    return false;
  }
  const EventBits_t bits = xEventGroupWaitBits(
      g_events, kGotIpBit, pdFALSE, pdTRUE, pdMS_TO_TICKS(timeout_ms));
  return (bits & kGotIpBit) != 0;
}

extern "C" uint8_t neon_wifi_last_disconnect_reason(void) {
  return g_last_disconnect_reason;
}

extern "C" const char* neon_wifi_current_ssid(void) { return effective_ssid(); }

extern "C" int8_t neon_wifi_rssi(void) {
  if (!g_sta_started) {
    return 0;
  }
  wifi_ap_record_t info = {};
  return esp_wifi_sta_get_ap_info(&info) == ESP_OK ? info.rssi : 0;
}

int neon_wifi_scan(NeonWifiScanEntry* out, int max_entries) {
  if (out == nullptr || max_entries <= 0) {
    return 0;
  }
  // Hosted UART init blocks ~20 s (or forever). Do not start a second
  // esp_wifi_init from SCAN while the radio task is still waiting.
  if (!netman::wifi_driver_ready()) {
    ESP_LOGW(kTag, "scan skipped: wifi driver not up");
    return 0;
  }
  ensure_events();
  netman::init_common();
  if (esp_netif_get_handle_from_ifkey("WIFI_STA_DEF") == nullptr) {
    esp_netif_create_default_wifi_sta();
  }
  if (!ensure_wifi_driver()) {
    return 0;
  }
  ensure_handlers();

  // Scanning needs the station interface up, even if we never associate.
  wifi_mode_t mode = WIFI_MODE_NULL;
  esp_wifi_get_mode(&mode);
  if (mode == WIFI_MODE_NULL || mode == WIFI_MODE_AP) {
    const wifi_mode_t want =
        mode == WIFI_MODE_AP ? WIFI_MODE_APSTA : WIFI_MODE_STA;
    if (esp_wifi_set_mode(want) != ESP_OK) {
      return 0;
    }
    const esp_err_t err = esp_wifi_start();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
      return 0;
    }
    g_sta_started = true;
  }

  wifi_scan_config_t scan = {};
  scan.show_hidden = true;
  if (esp_wifi_scan_start(&scan, /*block=*/true) != ESP_OK) {
    ESP_LOGW(kTag, "scan failed");
    return 0;
  }
  uint16_t found = static_cast<uint16_t>(max_entries);
  wifi_ap_record_t* recs = static_cast<wifi_ap_record_t*>(
      calloc(static_cast<size_t>(max_entries), sizeof(wifi_ap_record_t)));
  if (recs == nullptr) {
    esp_wifi_scan_stop();
    return 0;
  }
  int n = 0;
  if (esp_wifi_scan_get_ap_records(&found, recs) == ESP_OK) {
    for (uint16_t i = 0; i < found && n < max_entries; ++i) {
      if (recs[i].ssid[0] == '\0') {
        continue;  // hidden AP with no name to show
      }
      std::strncpy(out[n].ssid, reinterpret_cast<const char*>(recs[i].ssid),
                   sizeof(out[n].ssid) - 1);
      out[n].ssid[sizeof(out[n].ssid) - 1] = '\0';
      out[n].rssi = recs[i].rssi;
      out[n].open = recs[i].authmode == WIFI_AUTH_OPEN ? 1 : 0;
      out[n].channel = recs[i].primary;
      ++n;
    }
  }
  free(recs);

  // A scan while associated leaves the radio idle; nudge it back.
  if (g_connect_requested) {
    esp_wifi_connect();
  }
  return n;
}

extern "C" int neon_wifi_scan_json(char* buf, int cap) {
  if (buf == nullptr || cap < 3) {
    return 0;
  }
  constexpr int kMaxEntries = 24;
  NeonWifiScanEntry* entries = static_cast<NeonWifiScanEntry*>(
      calloc(kMaxEntries, sizeof(NeonWifiScanEntry)));
  const int found = entries != nullptr ? neon_wifi_scan(entries, kMaxEntries) : 0;

  int n = std::snprintf(buf, static_cast<size_t>(cap), "[");
  for (int i = 0; i < found && n < cap; ++i) {
    // SSIDs are user-controlled: escape the two characters that would
    // otherwise break out of a JSON string.
    char esc[2 * sizeof(entries[i].ssid)];
    size_t e = 0;
    for (const char* p = entries[i].ssid;
         *p != '\0' && e + 2 < sizeof(esc); ++p) {
      if (*p == '"' || *p == '\\') {
        esc[e++] = '\\';
      } else if (static_cast<unsigned char>(*p) < 0x20) {
        continue;  // drop control characters outright
      }
      esc[e++] = *p;
    }
    esc[e] = '\0';
    n += std::snprintf(
        buf + n, static_cast<size_t>(cap - n),
        "%s{\"ssid\":\"%s\",\"rssi\":%d,\"open\":%s,\"channel\":%u}",
        i != 0 ? "," : "", esc, static_cast<int>(entries[i].rssi),
        entries[i].open ? "true" : "false",
        static_cast<unsigned>(entries[i].channel));
  }
  free(entries);
  if (n >= cap - 1) {
    // Truncated mid-object: fall back to an empty array rather than
    // handing the editor malformed JSON.
    n = std::snprintf(buf, static_cast<size_t>(cap), "[]");
    return n;
  }
  n += std::snprintf(buf + n, static_cast<size_t>(cap - n), "]");
  return n;
}

#else  // !NEON_HAVE_WIFI — ESP32-P4 has no on-chip radio.

static const char* kTag = "wifi";

bool neon_wifi_has_credentials() { return false; }

void neon_wifi_start() {
  ESP_LOGI(kTag, "no on-chip WiFi on this target; skipping STA");
}

void neon_wifi_hold_station() {}

extern "C" void neon_wifi_apply_credentials(void) {}

extern "C" void neon_wifi_apply_credentials_later(void) {}

bool neon_wifi_sta_got_ip() { return false; }

bool neon_wifi_wait_ip(uint32_t) { return false; }

extern "C" uint8_t neon_wifi_last_disconnect_reason(void) { return 0; }

extern "C" const char* neon_wifi_current_ssid(void) { return ""; }

extern "C" int8_t neon_wifi_rssi(void) { return 0; }

int neon_wifi_scan(NeonWifiScanEntry*, int) { return 0; }

extern "C" int neon_wifi_scan_json(char* buf, int cap) {
  if (buf == nullptr || cap < 3) {
    return 0;
  }
  return std::snprintf(buf, static_cast<size_t>(cap), "[]");
}

#endif  // NEON_HAVE_WIFI
