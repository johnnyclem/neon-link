// Interface enumeration for Link's InterfaceScanner: the netlink build
// has exactly one candidate — the USB CDC-ECM bridge. Returning it only
// while the interface is up and addressed makes the scanner open/close
// the gateway on cable plug/unplug, the same behaviour the desktop
// platforms get from their interface lists.

#pragma once

#include <ableton/discovery/AsioTypes.hpp>
#include <cstdint>
#include <vector>

// daisy/netlink/src/usbnet_daisy.cpp: the bridge's IPv4 in host order,
// 0 while the interface is down or unaddressed.
extern "C" uint32_t neon_netlink_ipv4_hostorder();

namespace ableton
{
namespace platforms
{
namespace netdaisy
{

struct ScanIpIfAddrs
{
  std::vector<discovery::IpAddress> operator()()
  {
    std::vector<discovery::IpAddress> addrs;
    const uint32_t ip = neon_netlink_ipv4_hostorder();
    if (ip != 0)
    {
      addrs.push_back(discovery::IpAddressV4{ip});
    }
    return addrs;
  }
};

} // namespace netdaisy
} // namespace platforms
} // namespace ableton
