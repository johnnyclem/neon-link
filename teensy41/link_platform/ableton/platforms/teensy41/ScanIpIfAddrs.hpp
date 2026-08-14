// Interface enumeration for Link's InterfaceScanner: the Teensy 4.1 has
// exactly one candidate — native Ethernet. Returning it only while the
// link is up and addressed makes the scanner open/close the gateway on
// cable plug/unplug, the same behaviour the desktop platforms get from
// their interface lists.

#pragma once

#include <QNEthernet.h>

#include <ableton/discovery/AsioTypes.hpp>
#include <vector>

namespace ableton
{
namespace platforms
{
namespace teensy41
{

struct ScanIpIfAddrs
{
  std::vector<discovery::IpAddress> operator()()
  {
    std::vector<discovery::IpAddress> addrs;
    const IPAddress ip = qindesign::network::Ethernet.localIP();
    if (qindesign::network::Ethernet.linkState()
        && !(ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0))
    {
      addrs.push_back(discovery::IpAddressV4{
        discovery::IpAddressV4::bytes_type{ip[0], ip[1], ip[2], ip[3]}});
    }
    return addrs;
  }
};

} // namespace teensy41
} // namespace platforms
} // namespace ableton
