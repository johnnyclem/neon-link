#include "neon/client/bind.hpp"

#include <cctype>

namespace neon::client {
namespace {

bool ends_with_local(std::string_view s) {
  constexpr std::string_view k = ".local";
  if (s.size() < k.size()) {
    return false;
  }
  for (size_t i = 0; i < k.size(); ++i) {
    const char a = static_cast<char>(
        std::tolower(static_cast<unsigned char>(s[s.size() - k.size() + i])));
    if (a != k[i]) {
      return false;
    }
  }
  return true;
}

}  // namespace

std::string dns_label(std::string_view s) {
  if (ends_with_local(s)) {
    return std::string(s.substr(0, s.size() - 6));
  }
  return std::string(s);
}

std::string mdns_host(std::string_view label) {
  if (label.empty()) {
    return "neon-link.local";
  }
  if (ends_with_local(label)) {
    return std::string(label);
  }
  std::string out;
  out.reserve(label.size() + 6);
  out.append(label.begin(), label.end());
  out.append(".local");
  return out;
}

bool is_ipv4_literal(std::string_view s) {
  int dots = 0;
  int group = 0;
  bool digit = false;
  for (char c : s) {
    if (c == '.') {
      if (!digit || group > 255) {
        return false;
      }
      ++dots;
      group = 0;
      digit = false;
    } else if (c >= '0' && c <= '9') {
      group = group * 10 + (c - '0');
      digit = true;
      if (group > 255) {
        return false;
      }
    } else {
      return false;
    }
  }
  return digit && dots == 3 && group <= 255;
}

}  // namespace neon::client
