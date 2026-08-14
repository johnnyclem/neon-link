// Teensy 4.1 shadow of ableton/discovery/AsioTypes.hpp (see the
// upstream file for the contract). Maps the discovery type aliases onto
// the asio-free types in NetTypes.hpp — this directory precedes
// third_party/link/include on the include path, so every protocol
// header picks these up instead of asio.

#pragma once

#include <ableton/platforms/teensy41/NetTypes.hpp>

#include <algorithm>
#include <utility>

namespace ableton
{
namespace discovery
{

using IpAddress = ::neonnet::ip::address;
using IpAddressV4 = ::neonnet::ip::address_v4;
using IpAddressV6 = ::neonnet::ip::address_v6;
using UdpSocket = ::neonnet::ip::udp::socket;
using UdpEndpoint = ::neonnet::ip::udp::endpoint;

template <typename... Args>
inline IpAddress makeAddress(Args&&... args)
{
  return ::neonnet::ip::make_address(std::forward<Args>(args)...);
}

template <typename AddrType>
AddrType makeAddressFromBytes(const char* pAddr)
{
  typename AddrType::bytes_type bytes;
  std::copy(pAddr, pAddr + bytes.size(), begin(bytes));
  return AddrType{bytes};
}

template <typename AddrType>
AddrType makeAddressFromBytes(const char* pAddr, uint32_t scopeId)
{
  typename AddrType::bytes_type bytes;
  std::copy(pAddr, pAddr + bytes.size(), begin(bytes));
  return AddrType{bytes, scopeId};
}

} // namespace discovery
} // namespace ableton
