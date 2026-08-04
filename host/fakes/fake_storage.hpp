#pragma once

#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "hal/IStorage.hpp"

namespace fakes {

class FakeStorage final : public hal::IStorage {
 public:
  bool read_blob(const char* key, void* buf, size_t cap,
                 size_t* len) override {
    const auto it = blobs_.find(key);
    if (it == blobs_.end() || it->second.size() > cap) {
      return false;
    }
    std::memcpy(buf, it->second.data(), it->second.size());
    *len = it->second.size();
    return true;
  }

  bool write_blob(const char* key, const void* data, size_t len) override {
    const auto* p = static_cast<const uint8_t*>(data);
    blobs_[key] = std::vector<uint8_t>(p, p + len);
    return true;
  }

  std::map<std::string, std::vector<uint8_t>> blobs_;
};

}  // namespace fakes
