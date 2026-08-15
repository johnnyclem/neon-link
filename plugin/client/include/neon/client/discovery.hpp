#pragma once

#include <string>
#include <vector>

namespace neon::client {

// Portable browse result. The macOS dns_sd backend lands with the
// DeviceController (not this PR). Tests inject a fake list.
struct Discovered {
  std::string instance;
  std::string host;
  int port = 80;
  std::string device_name;  // TXT name, or empty
  std::string firmware;     // TXT fw
  std::string id;           // TXT id
  std::string kind;         // TXT kind; empty means neon-link
};

class DiscoveryBackend {
 public:
  virtual ~DiscoveryBackend() = default;
  virtual void start() = 0;
  virtual void stop() = 0;
  virtual std::vector<Discovered> snapshot() const = 0;
};

}  // namespace neon::client
