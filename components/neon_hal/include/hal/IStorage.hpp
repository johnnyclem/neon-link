#pragma once

#include <cstddef>
#include <cstdint>

namespace hal {

// Key-value blob persistence (NVS on the device, an in-memory map in host
// tests). Keys are short C strings.
class IStorage {
 public:
  virtual ~IStorage() = default;

  // Reads up to `cap` bytes into buf; sets *len to the stored size.
  // Returns false if the key does not exist or reading fails.
  virtual bool read_blob(const char* key, void* buf, size_t cap,
                         size_t* len) = 0;

  virtual bool write_blob(const char* key, const void* data, size_t len) = 0;
};

}  // namespace hal
