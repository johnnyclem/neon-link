#include "netman/net_manager.h"

#include <cstring>

#include <cstdio>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_eth.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "mdns.h"

#include "board_pins.h"

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

bool ethernet_start() {
  // AMYboard (and any board without a wired W5500) leaves eth pins at -1.
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

  // The W5500 has no burned-in MAC; derive one from the SoC.
  uint8_t mac_addr[6] = {};
  esp_read_mac(mac_addr, ESP_MAC_ETH);
  esp_eth_ioctl(handle, ETH_CMD_S_MAC_ADDR, mac_addr);

  // Route priority above WiFi STA (100) so the cable wins when present.
  esp_netif_inherent_config_t base = ESP_NETIF_INHERENT_DEFAULT_ETH();
  base.route_prio = 128;
  esp_netif_config_t netif_cfg = {
      .base = &base,
      .driver = nullptr,
      .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH,
  };
  g_eth_netif = esp_netif_new(&netif_cfg);
  ESP_ERROR_CHECK(
      esp_netif_attach(g_eth_netif, esp_eth_new_netif_glue(handle)));

  ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID,
                                             &on_eth_event, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP,
                                             &on_ip_event, nullptr));

  err = esp_eth_start(handle);
  if (err != ESP_OK) {
    ESP_LOGW(kTag, "esp_eth_start failed: %d", err);
    return false;
  }
  ESP_LOGI(kTag, "W5500 Ethernet started");
  return true;
}

bool g_ap_up = false;

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

void mdns_start() {
  if (mdns_init() != ESP_OK) {
    ESP_LOGW(kTag, "mDNS init failed");
    return;
  }
  mdns_hostname_set("neon-link");
  mdns_instance_name_set("NEON LINK");
  // Advertise the web editor so Finder/Bonjour/Android discovery tools
  // can resolve more than a bare hostname probe.
  if (mdns_service_add("NEON LINK", "_http", "_tcp", 80, nullptr, 0) !=
      ESP_OK) {
    ESP_LOGW(kTag, "mDNS HTTP service add failed (hostname still set)");
  }
  ESP_LOGI(kTag, "mDNS: neon-link.local (_http._tcp:80)");
}

bool ap_start() {
  if (g_ap_up) {
    return true;
  }

  wifi_mode_t mode = WIFI_MODE_NULL;
  const bool wifi_inited = esp_wifi_get_mode(&mode) == ESP_OK;
  if (!wifi_inited) {
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&init) != ESP_OK) {
      return false;
    }
    mode = WIFI_MODE_NULL;
  }
  esp_netif_create_default_wifi_ap();

  uint8_t mac[6] = {};
  esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
  wifi_config_t cfg = {};
  std::snprintf(reinterpret_cast<char*>(cfg.ap.ssid), sizeof(cfg.ap.ssid),
                "NEON-LINK-%02X%02X", mac[4], mac[5]);
  cfg.ap.ssid_len = 0;  // derive from string
  cfg.ap.channel = 1;
  cfg.ap.authmode = WIFI_AUTH_OPEN;  // setup network; editor sets STA creds
  cfg.ap.max_connection = 2;

  const wifi_mode_t new_mode =
      (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA) ? WIFI_MODE_APSTA
                                                         : WIFI_MODE_AP;
  if (esp_wifi_set_mode(new_mode) != ESP_OK ||
      esp_wifi_set_config(WIFI_IF_AP, &cfg) != ESP_OK) {
    return false;
  }
  if (!wifi_inited || mode == WIFI_MODE_NULL) {
    if (esp_wifi_start() != ESP_OK) {
      return false;
    }
  }
  g_ap_up = true;
  ESP_LOGI(kTag, "setup AP up: %s (192.168.4.1)",
           reinterpret_cast<char*>(cfg.ap.ssid));
  return true;
}

bool ap_is_up() { return g_ap_up; }

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
