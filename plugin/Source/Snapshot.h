#pragma once

#include <string>
#include <vector>

#include "neon/client/status.hpp"
#include "neon/config/model.hpp"

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

  bool has_config = false;
  neon::Config config{};
  neon::client::ConfigSecrets secrets{};
  uint32_t config_seq = 0;  // increments on each successful GET / PUT

  bool saving = false;
  bool last_save_ok = false;
  std::string save_message;
  uint32_t save_seq = 0;

  bool scanning = false;
  std::string scan_message;
  std::vector<neon::client::ScanResult> scan;

  neon::client::AudioChannels audio_channels;
  bool audio_refreshing = false;
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
