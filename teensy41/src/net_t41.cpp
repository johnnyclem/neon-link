#include "net_t41.h"

#include <QNEthernet.h>

#include <cstdio>
#include <cstring>

namespace net {
namespace {

using namespace qindesign::network;

// DHCP grace before self-assigning. Long enough for a sleepy home
// router, short enough that a direct laptop cable is usable quickly.
constexpr int64_t kDhcpGraceUs = 20 * 1000000ll;

// Static fallback when no DHCP server answers (documented in
// docs/TEENSY41.md so the editor can still be reached).
const IPAddress kFallbackIp{192, 168, 76, 10};
const IPAddress kFallbackMask{255, 255, 255, 0};
const IPAddress kFallbackGw{192, 168, 76, 1};

bool g_started = false;
bool g_fallback = false;
bool g_mdns_up = false;
int64_t g_started_at_us = 0;
char g_hostname[32] = "neon-link";

bool ip_valid(const IPAddress& ip) {
  return !(ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0);
}

void mdns_start() {
  if (g_mdns_up) {
    return;
  }
  if (MDNS.begin(g_hostname)) {
    MDNS.addService("_http", "_tcp", 80);
    g_mdns_up = true;
  }
}

}  // namespace

void init(const char* hostname) {
  set_hostname(hostname);
  Ethernet.setHostname(g_hostname);
  g_started = Ethernet.begin();  // non-blocking DHCP
  g_started_at_us = 0;
}

void set_hostname(const char* hostname) {
  if (hostname == nullptr || hostname[0] == '\0') {
    return;
  }
  std::snprintf(g_hostname, sizeof(g_hostname), "%s", hostname);
  if (g_mdns_up) {
    // QNEthernet's mDNS has no rename; restart the responder.
    MDNS.end();
    g_mdns_up = false;
    mdns_start();
  }
}

void poll(int64_t now_us) {
  Ethernet.loop();

  if (g_started_at_us == 0) {
    g_started_at_us = now_us;
  }
  if (!g_fallback && !has_ip() && link_up() &&
      now_us - g_started_at_us > kDhcpGraceUs) {
    // No DHCP answer on a live cable (direct to a laptop, most likely).
    Ethernet.begin(kFallbackIp, kFallbackMask, kFallbackGw);
    g_fallback = true;
  }
  if (has_ip()) {
    mdns_start();
  }
}

bool link_up() { return Ethernet.linkState(); }

bool has_ip() { return ip_valid(Ethernet.localIP()); }

void primary_ip(char* out, size_t cap) {
  if (cap == 0) {
    return;
  }
  out[0] = '\0';
  const IPAddress ip = Ethernet.localIP();
  if (ip_valid(ip)) {
    std::snprintf(out, cap, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  }
}

}  // namespace net
