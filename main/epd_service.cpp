#include "sdkconfig.h"
#include "tasks.h"

#include "app_state/config_store.h"
#include "app_state/timeline_bus.h"
#include "board_mac.h"
#include "board_pins.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "halesp/epd_5in79.hpp"
#include "neon/fixed_math.hpp"
#include "neon/gfx/epd_canvas.hpp"
#include "neon/transport.hpp"
#include "netman/net_manager.h"
#include "provision.h"
#include "wifi.h"

#include <cstdio>
#include <cstring>

#if CONFIG_NEON_BOARD_LINKSYNC_EPD

namespace {

const char* kTag = "epd";

uint32_t hash_text(uint32_t h, const char* p) {
  for (; p != nullptr && *p != '\0'; ++p) {
    h = h * 33u + static_cast<uint8_t>(*p);
  }
  return h;
}

// Text that actually appears on the glass. Beat / phase / animation
// never enter this — e-paper is not a metronome.
uint32_t fingerprint(const neon::LinkSyncPanelStatus& s) {
  uint32_t h = s.milli_bpm / 100u;  // matches the one-decimal BPM string
  h = h * 33u + (s.playing ? 1u : 0u);
  h = h * 33u + s.peers;
  h = h * 33u + (s.provisioned ? 1u : 0u);
  h = h * 33u + (s.wifi_up ? 1u : 0u);
  h = h * 33u + (s.setup_ap ? 1u : 0u);
  h = hash_text(h, s.title);
  h = hash_text(h, s.ssid);
  h = hash_text(h, s.ap_ssid);
  h = hash_text(h, s.ap_pass);
  h = hash_text(h, s.detail);
  return h;
}

void fill_status(neon::LinkSyncPanelStatus* s) {
  const neon::Config& cfg = neon_config();
  std::snprintf(s->title, sizeof(s->title), "%s", cfg.device_name);
  neon::TimelineSnapshot tl{};
  timeline_bus().read(tl);
  s->milli_bpm = neon::milli_bpm_from_mpb_us(
      tl.tempo_mpb_q32 != 0 ? ((tl.tempo_mpb_q32 + (1ull << 31)) >> 32)
                            : 500000ull);
  s->playing = tl.playing != 0;
  s->peers = tl.num_peers;
  s->provisioned = neon_wifi_has_credentials();
  s->wifi_up = neon_wifi_sta_got_ip();
  s->setup_ap = netman::ap_is_up();
  std::snprintf(s->ssid, sizeof(s->ssid), "%s", neon_wifi_current_ssid());
  std::snprintf(s->ap_pass, sizeof(s->ap_pass), "%s", cfg.ap_pass);
  if (s->setup_ap && netman::ap_ssid()[0] != '\0') {
    std::snprintf(s->ap_ssid, sizeof(s->ap_ssid), "%s", netman::ap_ssid());
  } else {
    uint8_t mac[6] = {};
    neon_read_unit_mac(mac);
    neon::ap_ssid_for(cfg, mac, s->ap_ssid, sizeof(s->ap_ssid));
  }

  const bool show_ap = (s->setup_ap || !s->provisioned) && s->ap_pass[0] != '\0';
  if (show_ap) {
    std::snprintf(s->detail, sizeof(s->detail), "http://192.168.4.1");
  } else if (neon_provision_active()) {
    std::snprintf(s->detail, sizeof(s->detail), "BLE PROV  ESP BLE Prov app");
  } else if (!s->provisioned) {
    std::snprintf(s->detail, sizeof(s->detail), "NO WIFI  wait for SoftAP");
  } else {
    std::snprintf(s->detail, sizeof(s->detail), "MIDI CLOCK  24 PPQN  TRS-A");
  }
}

void epd_task(void*) {
  if (!halesp::epd5in79_init(kPinDispSck, kPinDispMosi, kPinDispCs, kPinDispDc,
                             kPinDispRes, kPinEpdBusy, kPinEpdPwr)) {
    ESP_LOGE(kTag, "e-paper init failed");
    vTaskDelete(nullptr);
    return;
  }
  // 27 KB packed frame — must not live on the task stack (8 KB).
  auto* canvas = new neon::EpdCanvas();
  uint32_t last_fp = 0;
  bool asleep = false;
  int64_t next_ok_us = 0;

  for (;;) {
    neon::LinkSyncPanelStatus st{};
    fill_status(&st);
    const uint32_t fp = fingerprint(st);
    const int64_t now = esp_timer_get_time();
    if (fp != last_fp && now >= next_ok_us) {
      if (asleep) {
        halesp::epd5in79_awaken();
        asleep = false;
      }
      neon::render_linksync_panel(*canvas, st);
      ESP_LOGI(kTag, "paint ink=%d title=%s ap=%s", canvas->black_pixels(),
               st.title, st.setup_ap ? st.ap_pass : "-");
      // Fast 0xC7 needs Init_Fast() (temp load). Regular init + 0xC7
      // leaves the previous image, which after a white clear is blank.
      halesp::epd5in79_display(canvas->data(), /*fast=*/false);
      halesp::epd5in79_sleep();
      asleep = true;
      last_fp = fp;
      next_ok_us = now + 5000000;  // 5 s floor between text changes
    }
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

}  // namespace

void neon_start_epd_service() {
  xTaskCreatePinnedToCore(epd_task, "epd", 8192, nullptr, 3, nullptr, 0);
}

#else

void neon_start_epd_service() {}

#endif
