#pragma once

// Minimal OSC 1.0 codec for the /neon control surface. Pure logic,
// host-tested, no sockets — the ESP service moves the datagrams.
// Clean-room implementation of the subset the SolarOS evaluation
// proved out (docs/SOLAROS_PORTS_HANDOFF.md §4):
//
//  - Big-endian, 4-byte aligned. OSC-strings must be genuinely
//    NUL-padded to their 4-byte boundary — sloppy encoders are
//    rejected as malformed.
//  - Exactly one argument per message, typetags ",f" ",i" ",T" ",F".
//    Floats are isfinite-checked; ints are cast; T/F become 1/0.
//  - "#bundle" accepted only with the immediate timetag {0,1},
//    nesting depth <= 2, and at most kMaxMessages messages applied
//    per packet.
//  - Semantic misses (unknown address, rejected value) are counted,
//    never packet errors; only malformed encoding fails a packet.

#include <cstddef>
#include <cstdint>

namespace neon {
namespace osc {

constexpr size_t kMaxPacket = 512;
constexpr int kMaxMessages = 8;
constexpr int kMaxBundleDepth = 2;
constexpr size_t kMaxAddress = 64;

struct DispatchStats {
  int messages = 0;  // well-formed messages seen
  int applied = 0;
  int unknown_paths = 0;
  int rejected_values = 0;
};

// The sink decides what an address means: return 1 = applied,
// 0 = unknown path, -1 = value rejected.
using Sink = int (*)(void* ctx, const char* address, float value);

// Returns false only on malformed encoding (bad padding, bad typetag,
// oversized, non-immediate bundle timetag, too-deep nesting). Stats
// are filled either way.
bool parse_packet(const uint8_t* pkt, size_t len, Sink sink, void* ctx,
                  DispatchStats* stats);

// Outbound: single-argument messages. Return bytes written, 0 when the
// address is invalid or `cap` is too small. Addresses must start with
// '/', contain no empty components, and stay inside printable ASCII
// minus the OSC pattern characters (# * , ? [ ] { }).
bool address_valid(const char* address);
size_t encode_float(const char* address, float v, uint8_t* out, size_t cap);
size_t encode_int(const char* address, int32_t v, uint8_t* out, size_t cap);

}  // namespace osc
}  // namespace neon
