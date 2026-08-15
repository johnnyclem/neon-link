// Minimal IP address / endpoint types with the asio surface Ableton
// Link's protocol code actually touches (see the shadowed
// ableton/discovery/AsioTypes.hpp). No sockets, no io_context — the
// Teensy platform's Socket/Context provide those over QNEthernet.

#pragma once

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace neonnet
{
namespace ip
{

class address_v4
{
public:
  using bytes_type = std::array<uint8_t, 4>;

  address_v4() = default;
  explicit address_v4(const bytes_type& b)
    : mAddr((uint32_t(b[0]) << 24) | (uint32_t(b[1]) << 16)
            | (uint32_t(b[2]) << 8) | uint32_t(b[3]))
  {
  }
  // Host byte order, matching asio::ip::address_v4(uint_type).
  explicit address_v4(uint32_t hostOrder)
    : mAddr(hostOrder)
  {
  }

  static address_v4 any() { return address_v4{}; }
  static address_v4 loopback() { return address_v4{0x7F000001u}; }

  uint32_t to_uint() const { return mAddr; }
  bytes_type to_bytes() const
  {
    return {uint8_t(mAddr >> 24), uint8_t(mAddr >> 16), uint8_t(mAddr >> 8),
      uint8_t(mAddr)};
  }
  std::string to_string() const
  {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u", unsigned(mAddr >> 24) & 255u,
      unsigned(mAddr >> 16) & 255u, unsigned(mAddr >> 8) & 255u,
      unsigned(mAddr) & 255u);
    return buf;
  }
  bool is_loopback() const { return (mAddr >> 24) == 127u; }
  bool is_multicast() const { return (mAddr >> 28) == 0xEu; }

  friend bool operator==(const address_v4& a, const address_v4& b)
  {
    return a.mAddr == b.mAddr;
  }
  friend bool operator!=(const address_v4& a, const address_v4& b)
  {
    return !(a == b);
  }
  friend bool operator<(const address_v4& a, const address_v4& b)
  {
    return a.mAddr < b.mAddr;
  }

private:
  uint32_t mAddr = 0;
};

class address_v6
{
public:
  using bytes_type = std::array<uint8_t, 16>;

  address_v6() { mBytes.fill(0); }
  explicit address_v6(const bytes_type& b, uint32_t scope = 0)
    : mBytes(b)
    , mScope(scope)
  {
  }

  static address_v6 any() { return address_v6{}; }
  static address_v6 loopback()
  {
    bytes_type b{};
    b[15] = 1;
    return address_v6{b};
  }

  const bytes_type& to_bytes() const { return mBytes; }
  uint32_t scope_id() const { return mScope; }
  void scope_id(uint32_t scope) { mScope = scope; }
  bool is_loopback() const { return *this == loopback(); }
  bool is_link_local() const
  {
    return mBytes[0] == 0xFE && (mBytes[1] & 0xC0) == 0x80;
  }
  std::string to_string() const
  {
    // Uncompressed hex groups; enough for logs and map keys.
    char buf[46];
    std::snprintf(buf, sizeof(buf), "%x:%x:%x:%x:%x:%x:%x:%x",
      group(0), group(1), group(2), group(3), group(4), group(5), group(6),
      group(7));
    std::string s{buf};
    if (mScope != 0)
    {
      s += "%" + std::to_string(mScope);
    }
    return s;
  }

  friend bool operator==(const address_v6& a, const address_v6& b)
  {
    return a.mBytes == b.mBytes && a.mScope == b.mScope;
  }
  friend bool operator!=(const address_v6& a, const address_v6& b)
  {
    return !(a == b);
  }
  friend bool operator<(const address_v6& a, const address_v6& b)
  {
    return a.mBytes != b.mBytes ? a.mBytes < b.mBytes : a.mScope < b.mScope;
  }

private:
  unsigned group(int i) const
  {
    return (unsigned(mBytes[2 * i]) << 8) | mBytes[2 * i + 1];
  }

  bytes_type mBytes;
  uint32_t mScope = 0;
};

class address
{
public:
  address() = default;
  address(const address_v4& v4) // NOLINT: implicit like asio
    : mIsV4(true)
    , mV4(v4)
  {
  }
  address(const address_v6& v6) // NOLINT: implicit like asio
    : mIsV4(false)
    , mV6(v6)
  {
  }

  bool is_v4() const { return mIsV4; }
  bool is_v6() const { return !mIsV4; }
  address_v4 to_v4() const { return mV4; }
  address_v6 to_v6() const { return mV6; }
  bool is_loopback() const
  {
    return mIsV4 ? mV4.is_loopback() : mV6.is_loopback();
  }
  std::string to_string() const
  {
    return mIsV4 ? mV4.to_string() : mV6.to_string();
  }

  friend bool operator==(const address& a, const address& b)
  {
    if (a.mIsV4 != b.mIsV4)
    {
      return false;
    }
    return a.mIsV4 ? a.mV4 == b.mV4 : a.mV6 == b.mV6;
  }
  friend bool operator!=(const address& a, const address& b) { return !(a == b); }
  friend bool operator<(const address& a, const address& b)
  {
    if (a.mIsV4 != b.mIsV4)
    {
      return a.mIsV4; // v4 sorts before v6, arbitrarily but stably
    }
    return a.mIsV4 ? a.mV4 < b.mV4 : a.mV6 < b.mV6;
  }

private:
  bool mIsV4 = true;
  address_v4 mV4{};
  address_v6 mV6{};
};

// Parses the literals Link uses: dotted-quad v4, and v6 with optional
// "::" compression and a numeric "%scope" suffix.
inline address make_address(const char* str)
{
  if (std::strchr(str, ':') == nullptr)
  {
    unsigned a = 0, b = 0, c = 0, d = 0;
    std::sscanf(str, "%u.%u.%u.%u", &a, &b, &c, &d);
    return address_v4{
      address_v4::bytes_type{uint8_t(a), uint8_t(b), uint8_t(c), uint8_t(d)}};
  }

  uint32_t scope = 0;
  char body[64];
  std::strncpy(body, str, sizeof(body) - 1);
  body[sizeof(body) - 1] = '\0';
  if (char* pct = std::strchr(body, '%'))
  {
    scope = static_cast<uint32_t>(std::strtoul(pct + 1, nullptr, 10));
    *pct = '\0';
  }

  // Split on "::" and fill groups from both ends.
  uint16_t groups[8] = {};
  int head = 0;
  const char* p = body;
  const char* dc = std::strstr(body, "::");
  const char* headEnd = dc != nullptr ? dc : body + std::strlen(body);
  while (p < headEnd && head < 8)
  {
    groups[head++] = static_cast<uint16_t>(std::strtoul(p, nullptr, 16));
    const char* colon = std::strchr(p, ':');
    if (colon == nullptr || colon >= headEnd)
    {
      break;
    }
    p = colon + 1;
  }
  if (dc != nullptr)
  {
    uint16_t tail[8] = {};
    int nTail = 0;
    p = dc + 2;
    while (*p != '\0' && nTail < 8)
    {
      tail[nTail++] = static_cast<uint16_t>(std::strtoul(p, nullptr, 16));
      const char* colon = std::strchr(p, ':');
      if (colon == nullptr)
      {
        break;
      }
      p = colon + 1;
    }
    for (int i = 0; i < nTail; ++i)
    {
      groups[8 - nTail + i] = tail[i];
    }
  }

  address_v6::bytes_type bytes;
  for (int i = 0; i < 8; ++i)
  {
    bytes[2 * i] = uint8_t(groups[i] >> 8);
    bytes[2 * i + 1] = uint8_t(groups[i]);
  }
  return address_v6{bytes, scope};
}

inline address make_address(const std::string& s)
{
  return make_address(s.c_str());
}

namespace udp
{

class endpoint
{
public:
  endpoint() = default;
  endpoint(const ip::address& addr, uint16_t port)
    : mAddr(addr)
    , mPort(port)
  {
  }
  endpoint(const ip::address_v4& addr, uint16_t port)
    : mAddr(addr)
    , mPort(port)
  {
  }
  endpoint(const ip::address_v6& addr, uint16_t port)
    : mAddr(addr)
    , mPort(port)
  {
  }

  const ip::address& address() const { return mAddr; }
  void address(const ip::address& a) { mAddr = a; }
  uint16_t port() const { return mPort; }
  void port(uint16_t p) { mPort = p; }

  friend bool operator==(const endpoint& a, const endpoint& b)
  {
    return a.mPort == b.mPort && a.mAddr == b.mAddr;
  }
  friend bool operator!=(const endpoint& a, const endpoint& b)
  {
    return !(a == b);
  }
  friend bool operator<(const endpoint& a, const endpoint& b)
  {
    return a.mAddr != b.mAddr ? a.mAddr < b.mAddr : a.mPort < b.mPort;
  }

private:
  ip::address mAddr{};
  uint16_t mPort = 0;
};

// Placeholder so discovery::UdpSocket names a type; the Teensy platform
// Socket does not wrap one of these.
struct socket
{
};

} // namespace udp
} // namespace ip

// Truthiness-tested by every Link timer handler (`if (!e)`).
struct error_code
{
  int value = 0;
  explicit operator bool() const { return value != 0; }
  bool operator!() const { return value == 0; }
};

} // namespace neonnet

// Byte-order helpers for NetworkByteStreamSerializable. lwIP (via
// QNEthernet) defines the 16/32-bit names as macros in TUs that include
// it; only define what is not already there.
#ifndef htonl
inline uint32_t htonl(uint32_t x)
{
  return __builtin_bswap32(x);
}
inline uint32_t ntohl(uint32_t x)
{
  return __builtin_bswap32(x);
}
#endif
#ifndef htons
inline uint16_t htons(uint16_t x)
{
  return __builtin_bswap16(x);
}
inline uint16_t ntohs(uint16_t x)
{
  return __builtin_bswap16(x);
}
#endif
#ifndef htonll
#define htonll(x) __builtin_bswap64(x)
#endif
#ifndef ntohll
#define ntohll(x) __builtin_bswap64(x)
#endif
