#pragma once

#include "hal/IStorage.hpp"

namespace halesp {

// NVS-backed blob storage (namespace "neon"). nvs_flash_init() must have
// run before first use (app_main does this at boot).
class StorageNvs final : public hal::IStorage {
 public:
  bool read_blob(const char* key, void* buf, size_t cap, size_t* len) override;
  bool write_blob(const char* key, const void* data, size_t len) override;
};

}  // namespace halesp
