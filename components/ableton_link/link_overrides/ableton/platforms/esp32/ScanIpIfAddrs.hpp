#pragma once

// NEON LINK override: skip 0.0.0.0 and, when STA has a real address,
// hide the SoftAP (192.168.4.0/24) so Link Audio unicast is reachable
// from Live on the studio LAN. Upstream advertises every up netif.

#include <ableton/discovery/AsioTypes.hpp>
#include <arpa/inet.h>
#include <esp_idf_version.h>
#include <esp_netif.h>
#include <vector>

#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 5, 0)
#define esp_netif_next_unsafe esp_netif_next
#endif

namespace ableton
{
namespace platforms
{
namespace esp32
{

struct ScanIpIfAddrs
{
  std::vector<discovery::IpAddress> operator()()
  {
    std::vector<discovery::IpAddress> addrs;
    esp_netif_tcpip_exec(
      [](void* ctx) -> esp_err_t
      {
        auto& out = *static_cast<std::vector<discovery::IpAddress>*>(ctx);
        for (esp_netif_t* n = esp_netif_next_unsafe(nullptr); n != nullptr;
             n = esp_netif_next_unsafe(n))
        {
          if (!esp_netif_is_netif_up(n))
          {
            continue;
          }
          esp_netif_ip_info_t ip{};
          if (esp_netif_get_ip_info(n, &ip) != ESP_OK || ip.ip.addr == 0)
          {
            continue;
          }
          out.emplace_back(::asio::ip::address_v4(ntohl(ip.ip.addr)));
        }
        return ESP_OK;
      },
      &addrs);

    bool have_lan = false;
    for (const auto& a : addrs)
    {
      if (a.is_v4())
      {
        const uint32_t v = a.to_v4().to_uint();
        if ((v & 0xffffff00u) != 0xc0a80400u)
        {  // not 192.168.4.0/24
          have_lan = true;
          break;
        }
      }
    }
    if (have_lan)
    {
      std::vector<discovery::IpAddress> lan;
      for (const auto& a : addrs)
      {
        if (!a.is_v4() || (a.to_v4().to_uint() & 0xffffff00u) != 0xc0a80400u)
        {
          lan.push_back(a);
        }
      }
      return lan;
    }
    return addrs;
  }
};

} // namespace esp32
} // namespace platforms
} // namespace ableton
