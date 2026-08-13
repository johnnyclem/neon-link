#pragma once

#include <string>

#include "neon/client/status.hpp"

namespace neon::plugin {

enum class Reachability { Offline, Connecting, Online, Reconnecting };

struct Bind {
  std::string device_name;   // DNS label, no suffix
  std::string connect_host;  // getaddrinfo target
  std::string ip;            // last-good IPv4
};

struct Snapshot {
  Bind bind;
  Reachability reach = Reachability::Offline;
  neon::client::Status status;
  bool persist_lazy = false;
  std::string banner;
};

inline const char* reach_label(Reachability r) {
  switch (r) {
    case Reachability::Offline:
      return "OFFLINE";
    case Reachability::Connecting:
      return "CONNECTING…";
    case Reachability::Online:
      return nullptr;  // chips show LINK/RUN/STA
    case Reachability::Reconnecting:
      return "RECONNECTING…";
  }
  return "OFFLINE";
}

}  // namespace neon::plugin
