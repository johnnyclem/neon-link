#pragma once

#include <string>
#include <string_view>

namespace neon::client {

// Strip a single trailing ".local" (any case) if present. Never appends.
std::string dns_label(std::string_view s);

// label + ".local" only if `label` does not already end with ".local".
std::string mdns_host(std::string_view label);

// True when `s` looks like an IPv4 literal (digits and dots only, 4 parts).
bool is_ipv4_literal(std::string_view s);

}  // namespace neon::client
