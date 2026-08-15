#pragma once

#include <string>
#include <vector>

#include "Snapshot.h"
#include "neon/client/mic.hpp"

namespace neon::plugin {

struct MicSnapshot {
  Bind bind;
  int port = neon::client::kMicDefaultPort;
  Reachability reach = Reachability::Offline;
  neon::client::MicStatus status;
  bool has_config = false;
  neon::client::MicConfig config{};
  uint32_t config_seq = 0;
  std::vector<neon::client::MicSourceRow> sources;
  bool capturing = false;
  std::string banner;
  bool kind_mismatch = false;
};

}  // namespace neon::plugin
