#include "netman/net_manager.h"

#include <cstring>

#include <cstdio>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_app_desc.h"
#include "esp_eth.h"
#include "esp_eth_mac.h"
#include "esp_eth_phy.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mdns.h"
#include "sdkconfig.h"

#include "board_mac.h"
#include "board_pins.h"

#if CONFIG_ESP_WIFI_ENABLED || CONFIG_ESP_WIFI_REMOTE_ENABLED
// wifi_remote 0.14 on IDF 5.3 references this in WIFI_INIT_CONFIG_DEFAULT
// even when Kconfig did not emit it (depends on SPIRAM is flaky).
#ifndef CONFIG_WIFI_RMT_CACHE_TX_BUFFER_NUM
#define CONFIG_WIFI_RMT_CACHE_TX_BUFFER_NUM 32
#endif
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "esp_wifi_netif.h"
#define NEON_HAVE_WIFI 1
#else
#define NEON_HAVE_WIFI 0
#endif

namespace netman {

namespace {

const char* kTag = "net_manager";

neon::NetPreference g_preference;
esp_netif_t* g_eth_netif = nullptr;

void on_eth_event(void*, esp_event_base_t, int32_t id, void*) {
  switch (id) {
    case ETHERNET_EVENT_CONNECTED:
      ESP_LOGI(kTag, "ethernet link up");
      g_preference.eth_link(true);
      break;
    case ETHERNET_EVENT_DISCONNECTED:
      ESP_LOGW(kTag, "ethernet link down");
      g_preference.eth_link(false);
      break;
    default:
      break;
  }
}

void on_ip_event(void*, esp_event_base_t, int32_t id, void*) {
  if (id == IP_EVENT_ETH_GOT_IP) {
    ESP_LOGI(kTag, "ethernet got IP; preferring wired path");
    g_preference.eth_ip(true);
  }
}

}  // namespace

void init_common() {
  esp_err_t err = esp_netif_init();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_ERROR_CHECK(err);
  }
  err = esp_event_loop_create_default();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_ERROR_CHECK(err);
  }
}

#if NEON_HAVE_WIFI
bool g_wifi_driver = false;
int8_t g_tx_qdBm = 0;
int g_nearby = -1;

#if CONFIG_NEON_BOARD_LINKSYNC_C3OLED
// Super Mini C3: ceramic chip antenna is jammed against the 40 MHz xtal
// and GPIO21. Full TX power reflects into the PA, beacons vanish, the
// die cooks. Community fix is isolate GPIO20/21 and cap TX at 8.5 dBm
// (roryhay.es/blog/esp32-c3-super-mini-flaw).
void c3_isolate_antenna_gpios() {
  gpio_config_t io = {};
  io.pin_bit_mask = (1ull << 20) | (1ull << 21);
  io.mode = GPIO_MODE_INPUT;
  io.pull_down_en = GPIO_PULLDOWN_ENABLE;
  gpio_config(&io);
}

void c3_tune_tx() {
  const uint8_t proto =
      WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N;
  (void)esp_wifi_set_protocol(WIFI_IF_AP, proto);
  (void)esp_wifi_set_protocol(WIFI_IF_STA, proto);
  (void)esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20);
  (void)esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20);
  wifi_country_t country = {};
  country.cc[0] = '0';
  country.cc[1] = '1';
  country.schan = 1;
  country.nchan = 13;
  country.max_tx_power = 84;
  country.policy = WIFI_COUNTRY_POLICY_MANUAL;
  (void)esp_wifi_set_country(&country);
  // 8.5 dBm = 34 × 0.25 dBm. Higher values look stronger in the register
  // and weaker on the air because of the mismatch.
  constexpr int8_t kQdBm = 34;
  (void)esp_wifi_set_max_tx_power(kQdBm);
  (void)esp_wifi_get_max_tx_power(&g_tx_qdBm);
  ESP_LOGI(kTag, "C3 Super Mini TX %d qBm (%d.%d dBm)", g_tx_qdBm,
           g_tx_qdBm / 4, ((g_tx_qdBm % 4) * 25) / 10);
}

int c3_listen_probe() {
  if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK) {
    return -1;
  }
  (void)esp_wifi_set_ps(WIFI_PS_NONE);
  const esp_err_t start = esp_wifi_start();
  if (start != ESP_OK && start != ESP_ERR_INVALID_STATE) {
    ESP_LOGW(kTag, "listen probe start: %s", esp_err_to_name(start));
    return -1;
  }
  c3_tune_tx();
  wifi_scan_config_t scan = {};
  scan.ssid = nullptr;
  scan.bssid = nullptr;
  scan.channel = 0;
  scan.show_hidden = true;
  scan.scan_type = WIFI_SCAN_TYPE_ACTIVE;
  scan.scan_time.active.min = 80;
  scan.scan_time.active.max = 120;
  if (esp_wifi_scan_start(&scan, true) != ESP_OK) {
    ESP_LOGW(kTag, "listen probe scan failed");
    (void)esp_wifi_stop();
    return -1;
  }
  uint16_t n = 0;
  (void)esp_wifi_scan_get_ap_num(&n);
  wifi_ap_record_t recs[8] = {};
  uint16_t got = n > 8 ? 8 : n;
  if (got > 0) {
    (void)esp_wifi_scan_get_ap_records(&got, recs);
  }
  ESP_LOGI(kTag, "listen probe: %u nearby 2.4 GHz AP%s", n, n == 1 ? "" : "s");
  for (uint16_t i = 0; i < got; ++i) {
    ESP_LOGI(kTag, "  [%u] ch%u rssi=%d \"%s\"", i,
             recs[i].primary, static_cast<int>(recs[i].rssi),
             reinterpret_cast<char*>(recs[i].ssid));
  }
  (void)esp_wifi_stop();
  return static_cast<int>(n);
}
#endif

void wifi_after_start() {
#if CONFIG_NEON_BOARD_LINKSYNC_C3OLED
  c3_tune_tx();
#endif
}

bool wifi_driver_init() {
  if (g_wifi_driver) {
    return true;
  }
#if CONFIG_NEON_BOARD_LINKSYNC_C3OLED
  c3_isolate_antenna_gpios();
#endif
  wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
  const esp_err_t err = esp_wifi_init(&init);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "esp_wifi_init: %s (C6/Hosted transport down?)",
             esp_err_to_name(err));
    return false;
  }
  g_wifi_driver = true;
#if CONFIG_NEON_BOARD_LINKSYNC_C3OLED
  {
    uint32_t mask = 0;
    if (esp_wifi_get_event_mask(&mask) == ESP_OK) {
      mask &= ~WIFI_EVENT_MASK_AP_PROBEREQRECVED;
      (void)esp_wifi_set_event_mask(mask);
    }
  }
#endif
  return true;
}
#else
bool wifi_driver_init() { return false; }
void wifi_after_start() {}
#endif

bool attach_eth_netif(esp_eth_handle_t handle) {
  uint8_t mac_addr[6] = {};
  esp_read_mac(mac_addr, ESP_MAC_ETH);
  esp_eth_ioctl(handle, ETH_CMD_S_MAC_ADDR, mac_addr);

  esp_netif_inherent_config_t base = ESP_NETIF_INHERENT_DEFAULT_ETH();
  base.route_prio = 128;
  esp_netif_config_t netif_cfg = {
      .base = &base,
      .driver = nullptr,
      .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH,
  };
  g_eth_netif = esp_netif_new(&netif_cfg);
  if (g_eth_netif == nullptr) {
    return false;
  }
  if (esp_netif_attach(g_eth_netif, esp_eth_new_netif_glue(handle)) != ESP_OK) {
    return false;
  }
  ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID,
                                             &on_eth_event, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP,
                                             &on_ip_event, nullptr));
  return esp_eth_start(handle) == ESP_OK;
}

#if CONFIG_IDF_TARGET_ESP32P4 && CONFIG_ETH_USE_ESP32_EMAC
// Waveshare P4-Module-DEV-KIT / Function-EV: on-chip EMAC + IP101 RMII.
// Pins are IDF's ESP32-P4 EMAC defaults (MDC 31, MDIO 52, CLKIN 50,
// TX_EN 49, TXD 34/35, CRS/RX 28/29/30). PHY reset GPIO51, addr 1.
bool ethernet_start_emac_ip101() {
  eth_esp32_emac_config_t emac = ETH_ESP32_EMAC_DEFAULT_CONFIG();
  eth_mac_config_t mac_cfg = ETH_MAC_DEFAULT_CONFIG();
  eth_phy_config_t phy_cfg = ETH_PHY_DEFAULT_CONFIG();
  phy_cfg.phy_addr = 1;
  phy_cfg.reset_gpio_num = 51;

  esp_eth_mac_t* mac = esp_eth_mac_new_esp32(&emac, &mac_cfg);
  esp_eth_phy_t* phy = esp_eth_phy_new_ip101(&phy_cfg);
  if (mac == nullptr || phy == nullptr) {
    ESP_LOGW(kTag, "EMAC/IP101 alloc failed");
    return false;
  }
  esp_eth_config_t eth_cfg = ETH_DEFAULT_CONFIG(mac, phy);
  esp_eth_handle_t handle = nullptr;
  if (esp_eth_driver_install(&eth_cfg, &handle) != ESP_OK) {
    ESP_LOGW(kTag, "EMAC driver install failed");
    return false;
  }
  if (!attach_eth_netif(handle)) {
    ESP_LOGW(kTag, "EMAC netif/start failed");
    return false;
  }
  ESP_LOGI(kTag, "EMAC+IP101 Ethernet started (RJ45)");
  return true;
}
#endif

bool ethernet_start() {
#if CONFIG_IDF_TARGET_ESP32P4 && CONFIG_ETH_USE_ESP32_EMAC
  return ethernet_start_emac_ip101();
#endif
  // Custom PCB: SPI W5500. AMYboard leaves CS at -1.
  if (kPinEthCs < 0 || kPinEthSclk < 0) {
    ESP_LOGI(kTag, "no Ethernet pins; WiFi-only");
    return false;
  }
  spi_bus_config_t bus = {};
  bus.mosi_io_num = kPinEthMosi;
  bus.miso_io_num = kPinEthMiso;
  bus.sclk_io_num = kPinEthSclk;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGW(kTag, "SPI bus init failed: %d", err);
    return false;
  }

  spi_device_interface_config_t dev = {};
  dev.command_bits = 16;
  dev.address_bits = 8;
  dev.mode = 0;
  dev.clock_speed_hz = 20 * 1000 * 1000;
  dev.spics_io_num = kPinEthCs;
  dev.queue_size = 20;

  eth_w5500_config_t w5500 = ETH_W5500_DEFAULT_CONFIG(SPI2_HOST, &dev);
  w5500.int_gpio_num = kPinEthInt;

  eth_mac_config_t mac_cfg = ETH_MAC_DEFAULT_CONFIG();
  eth_phy_config_t phy_cfg = ETH_PHY_DEFAULT_CONFIG();
  phy_cfg.reset_gpio_num = kPinEthRst;

  esp_eth_mac_t* mac = esp_eth_mac_new_w5500(&w5500, &mac_cfg);
  esp_eth_phy_t* phy = esp_eth_phy_new_w5500(&phy_cfg);
  if (mac == nullptr || phy == nullptr) {
    ESP_LOGW(kTag, "W5500 MAC/PHY alloc failed");
    return false;
  }

  esp_eth_config_t eth_cfg = ETH_DEFAULT_CONFIG(mac, phy);
  esp_eth_handle_t handle = nullptr;
  err = esp_eth_driver_install(&eth_cfg, &handle);
  if (err != ESP_OK) {
    // Typical when no W5500 is wired up (bench/devkit builds).
    ESP_LOGW(kTag, "no W5500 detected (%d); running without Ethernet", err);
    return false;
  }

  if (!attach_eth_netif(handle)) {
    ESP_LOGW(kTag, "esp_eth_start failed");
    return false;
  }
  ESP_LOGI(kTag, "W5500 Ethernet started");
  return true;
}

volatile bool g_ap_up = false;

// Truncating copy into a fixed field. Deliberately not snprintf("%s"):
// with an unbounded source GCC's -Wformat-truncation cannot prove the
// result fits, and ESP-IDF promotes that to an error.
static void copy_str(char* dst, size_t cap, const char* src) {
  if (dst == nullptr || cap == 0) {
    return;
  }
  if (src == nullptr) {
    dst[0] = '\0';
    return;
  }
  const size_t slen = std::strlen(src);
  const size_t n = slen < cap - 1 ? slen : cap - 1;
  std::memcpy(dst, src, n);
  dst[n] = '\0';
}


bool ip_from_ifkey(const char* ifkey, char* buf, size_t len) {
  if (buf == nullptr || len == 0) {
    return false;
  }
  buf[0] = '\0';
  esp_netif_t* netif = esp_netif_get_handle_from_ifkey(ifkey);
  if (netif == nullptr) {
    return false;
  }
  esp_netif_ip_info_t info = {};
  if (esp_netif_get_ip_info(netif, &info) != ESP_OK || info.ip.addr == 0) {
    return false;
  }
  std::snprintf(buf, len, IPSTR, IP2STR(&info.ip));
  return true;
}

char g_hostname[32] = "neon-link";
char g_mdns_fw[24] = "unknown";
char g_mdns_id[8] = {};
bool g_mdns_up = false;
bool g_mdns_svc = false;

void mdns_device_id(char* out, size_t cap) {
  uint8_t mac[6] = {};
  neon_read_unit_mac(mac);
  std::snprintf(out, cap, "%02x%02x%02x", mac[3], mac[4], mac[5]);
}

void mdns_refresh_txt() {
  if (!g_mdns_svc) {
    return;
  }
  (void)mdns_service_txt_item_set("_http", "_tcp", "name", g_hostname);
}

void mdns_set_hostname(const char* hostname) {
  if (hostname == nullptr || hostname[0] == '\0') {
    return;
  }
  copy_str(g_hostname, sizeof(g_hostname), hostname);
  if (!g_mdns_up) {
    return;
  }
  mdns_hostname_set(g_hostname);
  mdns_instance_name_set(g_hostname);
  if (g_mdns_svc) {
    (void)mdns_service_instance_name_set("_http", "_tcp", g_hostname);
    mdns_refresh_txt();
  }
  ESP_LOGI(kTag, "mDNS hostname now %s.local", g_hostname);
}

void mdns_start(const char* hostname) {
  if (hostname != nullptr && hostname[0] != '\0') {
    copy_str(g_hostname, sizeof(g_hostname), hostname);
  }
  if (mdns_init() != ESP_OK) {
    ESP_LOGW(kTag, "mDNS init failed");
    return;
  }
  g_mdns_up = true;
  mdns_hostname_set(g_hostname);
  mdns_instance_name_set(g_hostname);

  const esp_app_desc_t* desc = esp_app_get_description();
  if (desc != nullptr && desc->version[0] != '\0') {
    copy_str(g_mdns_fw, sizeof(g_mdns_fw), desc->version);
  }
  mdns_device_id(g_mdns_id, sizeof(g_mdns_id));

  mdns_txt_item_t txt[] = {
      {"path", "/"},
      {"fw", g_mdns_fw},
      {"name", g_hostname},
      {"id", g_mdns_id},
  };
  // Instance name is the DNS-safe device_name so two modules on one LAN
  // are distinguishable once the user has renamed them.
  if (mdns_service_add(g_hostname, "_http", "_tcp", 80, txt,
                       sizeof(txt) / sizeof(txt[0])) != ESP_OK) {
    ESP_LOGW(kTag, "mDNS HTTP service add failed (hostname still set)");
  } else {
    g_mdns_svc = true;
  }
  ESP_LOGI(kTag, "mDNS: %s.local (_http._tcp:80 id=%s)", g_hostname,
           g_mdns_id);
}

char g_ap_ssid[33] = {};
bool g_ap_handlers = false;
esp_netif_t* g_ap_netif = nullptr;
bool g_ap_lwip_up = false;

// Who starts (netif_adds) the AP interface on WIFI_EVENT_AP_START.
//
// On native Wi-Fi, main/wifi.cpp calls esp_netif_create_default_wifi_sta(),
// which installs IDF's *shared* default handler set — and that set includes
// WIFI_EVENT_AP_START -> wifi_default_action_ap_start (esp_wifi/src/
// wifi_default.c). That handler already starts our AP netif, so if
// on_ap_event ALSO starts it, lwIP asserts "netif already added" (netif.c)
// and the chip reboots — which is exactly what a wrong Wi-Fi password
// (STA fails -> setup-AP fallback) triggered. So on native we must NOT add
// it a second time; we only track status.
//
// On Hosted (C6 over esp_wifi_remote: P4 LCD / Tab5) that shared STA path is
// not used, no default AP handler exists, and we must add it ourselves.
#if CONFIG_NEON_BOARD_LINKSYNC_P4LCD || CONFIG_NEON_BOARD_LINKSYNC_TAB5
constexpr bool kApSelfStartsNetif = true;
#else
constexpr bool kApSelfStartsNetif = false;
#endif

#if NEON_HAVE_WIFI
void ap_netif_start_once() {
  if (g_ap_lwip_up || g_ap_netif == nullptr) {
    return;
  }
  auto* driver =
      static_cast<wifi_netif_driver_t>(esp_netif_get_io_driver(g_ap_netif));
  uint8_t mac[6] = {};
  if (driver != nullptr && esp_wifi_get_if_mac(driver, mac) == ESP_OK) {
    (void)esp_netif_set_mac(g_ap_netif, mac);
  }
  if (driver != nullptr && esp_wifi_is_if_ready_when_started(driver)) {
    (void)esp_wifi_register_if_rxcb(driver, esp_netif_receive, g_ap_netif);
  }
  (void)esp_netif_action_start(g_ap_netif, WIFI_EVENT, WIFI_EVENT_AP_START,
                               nullptr);
  g_ap_lwip_up = true;
}

void on_ap_event(void*, esp_event_base_t base, int32_t id, void* event_data) {
  if (id == WIFI_EVENT_AP_START) {
    const bool first = !g_ap_lwip_up;
    if (kApSelfStartsNetif) {
      ap_netif_start_once();
    } else {
      // IDF's default WIFI_EVENT_AP_START handler already netif_added the AP
      // interface; adding it again here would assert. Just record it.
      g_ap_lwip_up = true;
    }
    g_ap_up = true;
    if (first) {
      ESP_LOGI(kTag, "SoftAP started: %s at 192.168.4.1", g_ap_ssid);
    }
  } else if (id == WIFI_EVENT_AP_STOP) {
    if (kApSelfStartsNetif && g_ap_lwip_up && g_ap_netif != nullptr) {
      (void)esp_netif_action_stop(g_ap_netif, base, id, event_data);
    }
    g_ap_lwip_up = false;
    g_ap_up = false;
    ESP_LOGW(kTag, "SoftAP stopped");
  } else if (id == WIFI_EVENT_AP_STACONNECTED) {
    auto* ev = static_cast<wifi_event_ap_staconnected_t*>(event_data);
    if (ev != nullptr) {
      ESP_LOGI(kTag, "AP client joined " MACSTR " aid=%u", MAC2STR(ev->mac),
               ev->aid);
    }
  } else if (id == WIFI_EVENT_AP_STADISCONNECTED) {
    auto* ev = static_cast<wifi_event_ap_stadisconnected_t*>(event_data);
    if (ev != nullptr) {
      ESP_LOGW(kTag, "AP client left " MACSTR " aid=%u", MAC2STR(ev->mac),
               ev->aid);
    }
  } else if (id == WIFI_EVENT_AP_PROBEREQRECVED) {
    auto* ev = static_cast<wifi_event_ap_probe_req_rx_t*>(event_data);
    if (ev != nullptr) {
      ESP_LOGI(kTag, "AP probe " MACSTR " rssi=%d", MAC2STR(ev->mac),
               ev->rssi);
    }
  }
}

bool ap_start(const ApParams& params) {
  // Do not use esp_netif_create_default_wifi_ap() on Hosted: the C6
  // posts WIFI_EVENT_AP_START twice per wifi_start, and the default
  // glue netif_adds on each (lwIP "netif already added" → reboot).
  if (g_ap_netif == nullptr) {
    esp_netif_config_t ncfg = ESP_NETIF_DEFAULT_WIFI_AP();
    g_ap_netif = esp_netif_new(&ncfg);
    if (g_ap_netif == nullptr ||
        esp_netif_attach_wifi_ap(g_ap_netif) != ESP_OK) {
      ESP_LOGE(kTag, "WIFI_AP_DEF create/attach failed");
      g_ap_netif = nullptr;
      return false;
    }
  }
  if (!wifi_driver_init()) {
    return false;
  }
  // Native boards start the AP netif — and with it the DHCP server — from
  // the shared default WIFI_EVENT handler set that
  // esp_netif_create_default_wifi_sta() installs (see kApSelfStartsNetif:
  // on_ap_event deliberately does NOT add the netif a second time). The
  // setup-AP-only path never runs main/wifi.cpp's neon_wifi_start() (it
  // early-returns with no credentials), and BLE provisioning can fail
  // before creating STA too — so without this the AP beacons but hands out
  // no leases and clients hang on "obtaining IP address". Create the STA
  // netif here so the handler exists; STA stays dormant in AP-only mode.
  if (!kApSelfStartsNetif &&
      esp_netif_get_handle_from_ifkey("WIFI_STA_DEF") == nullptr) {
    esp_netif_create_default_wifi_sta();
  }
#if CONFIG_NEON_BOARD_LINKSYNC_C3OLED
  if (g_nearby < 0) {
    g_nearby = c3_listen_probe();
  }
#endif
  wifi_mode_t mode = WIFI_MODE_NULL;
  if (esp_wifi_get_mode(&mode) != ESP_OK) {
    mode = WIFI_MODE_NULL;
  }
  if (!g_ap_handlers) {
    if (esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &on_ap_event,
                                   nullptr) == ESP_OK) {
      g_ap_handlers = true;
    }
  }

  wifi_config_t cfg = {};
  const char* ssid = (params.ssid != nullptr && params.ssid[0] != '\0')
                         ? params.ssid
                         : "NEON-LINK";
  copy_str(reinterpret_cast<char*>(cfg.ap.ssid), sizeof(cfg.ap.ssid), ssid);
  // Hosted 1.x on the factory C6 treats ssid_len=0 as a zero-length SSID
  // (no beacons). Native IDF would strlen() it. Always send the length.
  cfg.ap.ssid_len = static_cast<uint8_t>(std::strlen(
      reinterpret_cast<char*>(cfg.ap.ssid)));
  cfg.ap.channel = params.channel != 0 ? params.channel : 1;
  cfg.ap.max_connection = 4;
  cfg.ap.ssid_hidden = params.hidden ? 1 : 0;
  cfg.ap.beacon_interval = 100;
  // WPA2 needs an 8-character key; anything shorter stays an open network
  // rather than silently failing to start.
  const bool secured = params.require_pass && params.pass != nullptr &&
                       std::strlen(params.pass) >= 8;
  if (secured) {
    copy_str(reinterpret_cast<char*>(cfg.ap.password),
             sizeof(cfg.ap.password), params.pass);
    cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    cfg.ap.pairwise_cipher = WIFI_CIPHER_TYPE_CCMP;
    cfg.ap.pmf_cfg.capable = true;
    cfg.ap.pmf_cfg.required = false;
  } else {
    cfg.ap.authmode = WIFI_AUTH_OPEN;
  }
  copy_str(g_ap_ssid, sizeof(g_ap_ssid), ssid);
  ESP_LOGI(kTag, "SoftAP config SSID=%s auth=%s pass=%s", g_ap_ssid,
           secured ? "WPA2" : "open",
           secured ? reinterpret_cast<char*>(cfg.ap.password) : "(none)");

  // Setup AP must be AP-only. APSTA plus a scanning/retrying STA hops
  // the beacon channel and phones lose the network after a brief join.
  const bool sta_was_on = mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA;
  const wifi_mode_t new_mode =
      (params.keep_sta && sta_was_on) ? WIFI_MODE_APSTA : WIFI_MODE_AP;
  if (!params.keep_sta && sta_was_on) {
    (void)esp_wifi_disconnect();
    const esp_err_t stop_err = esp_wifi_stop();
    if (stop_err != ESP_OK && stop_err != ESP_ERR_WIFI_NOT_STARTED) {
      ESP_LOGW(kTag, "esp_wifi_stop: %s", esp_err_to_name(stop_err));
    }
  }

  // Modem sleep + SoftAP = missed beacons. Link also wants the radio awake.
  (void)esp_wifi_set_ps(WIFI_PS_NONE);

  if (esp_wifi_set_mode(new_mode) != ESP_OK ||
      esp_wifi_set_config(WIFI_IF_AP, &cfg) != ESP_OK) {
    return false;
  }
  g_ap_up = false;
  const esp_err_t start_err = esp_wifi_start();
  ESP_LOGI(kTag, "esp_wifi_start: %s", esp_err_to_name(start_err));
  if (start_err != ESP_OK && start_err != ESP_ERR_INVALID_STATE &&
      start_err != ESP_ERR_WIFI_NOT_STARTED) {
    ESP_LOGW(kTag, "esp_wifi_start failed; C6 not beaconing");
    return false;
  }
  wifi_after_start();
  for (int i = 0; i < 50 && !g_ap_up; ++i) {
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  if (!g_ap_up) {
    ESP_LOGE(kTag,
             "no WIFI_EVENT_AP_START after 5 s — Hosted accepted start "
             "but the C6 is not beaconing (%s, ch %u)",
             g_ap_ssid, static_cast<unsigned>(cfg.ap.channel));
    return false;
  }
  ESP_LOGI(kTag, "setup AP up: %s (%s%s, ch %u, %s) at 192.168.4.1", g_ap_ssid,
           secured ? "WPA2" : "open", params.hidden ? ", hidden" : "",
           static_cast<unsigned>(cfg.ap.channel),
           new_mode == WIFI_MODE_AP ? "AP-only" : "APSTA");
  return true;
}

#else  // !NEON_HAVE_WIFI

bool ap_start(const ApParams&) {
  ESP_LOGI(kTag, "no on-chip WiFi; setup AP skipped");
  return false;
}

#endif  // NEON_HAVE_WIFI

bool ap_is_up() { return g_ap_up; }

#if NEON_HAVE_WIFI
int8_t wifi_tx_qdBm() { return g_tx_qdBm; }
int wifi_nearby_count() { return g_nearby; }
#else
int8_t wifi_tx_qdBm() { return 0; }
int wifi_nearby_count() { return -1; }
#endif

const char* ap_ssid() { return g_ap_up ? g_ap_ssid : ""; }

bool primary_ip(char* buf, size_t len) {
  if (buf == nullptr || len == 0) {
    return false;
  }
  buf[0] = '\0';
  // Prefer the interface the preference machine considers active.
  switch (g_preference.active()) {
    case neon::ActiveNet::kEthernet:
      if (ip_from_ifkey("ETH_DEF", buf, len)) {
        return true;
      }
      break;
    case neon::ActiveNet::kWifi:
      if (ip_from_ifkey("WIFI_STA_DEF", buf, len)) {
        return true;
      }
      break;
    default:
      break;
  }
  // Fallbacks: try each interface even if preference lagged an event.
  if (ip_from_ifkey("ETH_DEF", buf, len)) {
    return true;
  }
  if (ip_from_ifkey("WIFI_STA_DEF", buf, len)) {
    return true;
  }
  if (g_ap_up && ip_from_ifkey("WIFI_AP_DEF", buf, len)) {
    return true;
  }
  if (g_ap_up) {
    std::snprintf(buf, len, "192.168.4.1");
    return true;
  }
  return false;
}

neon::NetPreference& preference() { return g_preference; }

}  // namespace netman
