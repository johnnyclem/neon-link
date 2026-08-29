#include "neon/osc/codec.hpp"

#include <cmath>
#include <cstring>

namespace neon {
namespace osc {

namespace {

uint32_t read_u32(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 24) |
         (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

void write_u32(uint8_t* p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v >> 24);
  p[1] = static_cast<uint8_t>(v >> 16);
  p[2] = static_cast<uint8_t>(v >> 8);
  p[3] = static_cast<uint8_t>(v);
}

// Reads an OSC-string at `off`; on success advances `off` past the
// 4-byte-aligned padding and returns the string start. The padding
// must actually be NUL bytes — anything else is a malformed packet.
const char* read_string(const uint8_t* pkt, size_t len, size_t* off) {
  const size_t start = *off;
  size_t end = start;
  while (end < len && pkt[end] != '\0') {
    ++end;
  }
  if (end >= len) {
    return nullptr;  // unterminated
  }
  const size_t padded = ((end - start) + 4) & ~static_cast<size_t>(3);
  if (start + padded > len) {
    return nullptr;
  }
  for (size_t i = end; i < start + padded; ++i) {
    if (pkt[i] != '\0') {
      return nullptr;
    }
  }
  *off = start + padded;
  return reinterpret_cast<const char*>(pkt + start);
}

bool parse_one(const uint8_t* pkt, size_t len, Sink sink, void* ctx,
               DispatchStats* stats);

bool parse_bundle(const uint8_t* pkt, size_t len, Sink sink, void* ctx,
                  DispatchStats* stats, int depth) {
  if (depth > kMaxBundleDepth) {
    return false;
  }
  // "#bundle\0" + 8-byte timetag; only the immediate tag {0,1} is
  // accepted — this box does not schedule.
  if (len < 16 || read_u32(pkt + 8) != 0 || read_u32(pkt + 12) != 1) {
    return false;
  }
  size_t off = 16;
  while (off < len) {
    if (off + 4 > len) {
      return false;
    }
    const uint32_t sz = read_u32(pkt + off);
    off += 4;
    if (sz == 0 || (sz & 3) != 0 || off + sz > len) {
      return false;
    }
    if (stats->messages >= kMaxMessages) {
      return true;  // cap reached; ignore the rest, packet still valid
    }
    const uint8_t* el = pkt + off;
    if (sz >= 8 && std::memcmp(el, "#bundle", 8) == 0) {
      if (!parse_bundle(el, sz, sink, ctx, stats, depth + 1)) {
        return false;
      }
    } else if (!parse_one(el, sz, sink, ctx, stats)) {
      return false;
    }
    off += sz;
  }
  return true;
}

bool parse_one(const uint8_t* pkt, size_t len, Sink sink, void* ctx,
               DispatchStats* stats) {
  size_t off = 0;
  const char* address = read_string(pkt, len, &off);
  if (address == nullptr || address[0] != '/' ||
      std::strlen(address) >= kMaxAddress) {
    return false;
  }
  const char* types = read_string(pkt, len, &off);
  if (types == nullptr || types[0] != ',' || types[1] == '\0' ||
      types[2] != '\0') {
    return false;  // exactly one argument
  }

  float value = 0.0f;
  switch (types[1]) {
    case 'f': {
      if (off + 4 > len) {
        return false;
      }
      const uint32_t bits = read_u32(pkt + off);
      off += 4;
      std::memcpy(&value, &bits, sizeof(value));
      ++stats->messages;
      if (!std::isfinite(value)) {
        ++stats->rejected_values;
        return true;
      }
      break;
    }
    case 'i': {
      if (off + 4 > len) {
        return false;
      }
      value = static_cast<float>(static_cast<int32_t>(read_u32(pkt + off)));
      off += 4;
      ++stats->messages;
      break;
    }
    case 'T':
      value = 1.0f;
      ++stats->messages;
      break;
    case 'F':
      value = 0.0f;
      ++stats->messages;
      break;
    default:
      return false;
  }
  if (off != len) {
    return false;  // trailing bytes = not the single-argument shape
  }

  const int r = sink != nullptr ? sink(ctx, address, value) : 0;
  if (r > 0) {
    ++stats->applied;
  } else if (r == 0) {
    ++stats->unknown_paths;
  } else {
    ++stats->rejected_values;
  }
  return true;
}

size_t string_padded(const char* s) {
  return (std::strlen(s) + 4) & ~static_cast<size_t>(3);
}

size_t encode_one(const char* address, char type, uint32_t payload,
                  uint8_t* out, size_t cap) {
  if (!address_valid(address)) {
    return 0;
  }
  const size_t addr_pad = string_padded(address);
  const size_t total = addr_pad + 4 /* ",f\0\0" */ + 4;
  if (cap < total) {
    return 0;
  }
  std::memset(out, 0, total);
  std::memcpy(out, address, std::strlen(address));
  out[addr_pad] = ',';
  out[addr_pad + 1] = static_cast<uint8_t>(type);
  write_u32(out + addr_pad + 4, payload);
  return total;
}

}  // namespace

bool parse_packet(const uint8_t* pkt, size_t len, Sink sink, void* ctx,
                  DispatchStats* stats) {
  DispatchStats local;
  if (stats == nullptr) {
    stats = &local;
  }
  *stats = DispatchStats{};
  if (pkt == nullptr || len == 0 || (len & 3) != 0 || len > kMaxPacket) {
    return false;
  }
  if (len >= 8 && std::memcmp(pkt, "#bundle", 8) == 0) {
    return parse_bundle(pkt, len, sink, ctx, stats, 1);
  }
  return parse_one(pkt, len, sink, ctx, stats);
}

bool address_valid(const char* address) {
  if (address == nullptr || address[0] != '/' || address[1] == '\0') {
    return false;
  }
  if (std::strlen(address) >= kMaxAddress) {
    return false;
  }
  char prev = '\0';
  for (const char* p = address; *p != '\0'; ++p) {
    const char c = *p;
    if (c < 0x21 || c > 0x7e) {
      return false;
    }
    if (c == '#' || c == '*' || c == ',' || c == '?' || c == '[' ||
        c == ']' || c == '{' || c == '}') {
      return false;
    }
    if (c == '/' && prev == '/') {
      return false;  // empty component
    }
    prev = c;
  }
  return prev != '/';
}

size_t encode_float(const char* address, float v, uint8_t* out, size_t cap) {
  if (!std::isfinite(v)) {
    return 0;
  }
  uint32_t bits = 0;
  std::memcpy(&bits, &v, sizeof(bits));
  return encode_one(address, 'f', bits, out, cap);
}

size_t encode_int(const char* address, int32_t v, uint8_t* out, size_t cap) {
  return encode_one(address, 'i', static_cast<uint32_t>(v), out, cap);
}

}  // namespace osc
}  // namespace neon
