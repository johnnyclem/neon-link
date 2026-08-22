#pragma once

// Neon Sync wire format (docs/NEON_SYNC.md §3). Fixed-layout little-endian
// messages, every one well under a single MTU. Serialization is explicit
// byte-at-a-time — no struct punning, no host-endianness assumptions — so
// the same code is correct on the ESP targets, the host test build, and
// (later) the plugin bridge.

#include <cstddef>
#include <cstdint>

namespace nsync {

// "NSYN" as the first four bytes on the wire.
inline constexpr uint8_t kMagic[4] = {'N', 'S', 'Y', 'N'};
inline constexpr uint8_t kProtoVersion = 1;

// Our own multicast group and port — deliberately distinct from Ableton
// Link's (224.76.78.75:20808). 239/8 is the IPv4 organization-local scope.
inline constexpr char kMulticastGroup[] = "239.77.83.78";
inline constexpr uint16_t kPort = 20809;

// Large enough for every message with headroom for future fields.
inline constexpr size_t kMaxPacket = 256;

enum class MsgType : uint8_t {
  kAnnounce = 1,  // multicast: session state + sender context
  kBye = 2,       // multicast: clean shutdown, header only
  kPing = 3,      // unicast: clock measurement request
  kPong = 4,      // unicast: clock measurement reply
  kTsfHint = 5,   // multicast: sender's (bssid, tsf, local) sample
};

struct Header {
  uint8_t proto_ver = kProtoVersion;
  MsgType type = MsgType::kAnnounce;
  uint16_t flags = 0;
  uint64_t node_id = 0;
  uint64_t session_id = 0;
};

// The session's shared beat grid. Anchored in *session time* (a clock that
// every peer maps onto its own local microsecond clock via its ghost
// offset), versioned with a Lamport counter so last-writer-wins merging is
// a pure function of the state itself. Deliberately isomorphic to
// neon::TimelineSnapshot: Q32.32 µs-per-beat, Q32.32 signed beats — no
// doubles on the wire.
struct TimelineState {
  uint64_t seq = 0;     // Lamport version; ties broken by writer
  uint64_t writer = 0;  // node id of the last writer
  uint64_t origin_session_us = 0;
  int64_t beat_at_origin_q32 = 0;
  uint64_t tempo_mpb_q32 = 0;  // microseconds per beat, Q32.32
  uint32_t quantum_mb = 4000;  // loop length in milli-beats
};

// Transport intent, versioned independently of the timeline so a tempo
// edit and a start/stop can never clobber each other. `playing` takes
// effect at `toggle_session_us` (the writer commits at its own quantum
// boundary; readers before that instant report the previous state).
struct TransportState {
  uint64_t seq = 0;
  uint64_t writer = 0;
  uint64_t toggle_session_us = 0;
  uint8_t playing = 1;
};

struct SessionState {
  TimelineState tl;
  TransportState tr;
};

struct AnnounceMsg {
  SessionState state;
  // Sender's session↔local mapping: session_us = local_us + ghost. A
  // receiver with a clock offset to the sender derives its own ghost from
  // this (docs/NEON_SYNC.md §4.2).
  int64_t ghost_offset_us = 0;
  uint64_t sender_local_tx_us = 0;  // coarse offset seed before ping/pong
  uint32_t ttl_ms = 0;              // peer expires this long after last rx
};

struct PingMsg {
  uint64_t t1_local_us = 0;
};

struct PongMsg {
  uint64_t t1_echo_us = 0;      // requester's t1, verbatim
  uint64_t t2_remote_rx_us = 0; // responder clock at receipt
  uint64_t t3_remote_tx_us = 0; // responder clock at reply
};

struct TsfHintMsg {
  uint8_t bssid[6] = {0, 0, 0, 0, 0, 0};
  uint64_t tsf_us = 0;
  uint64_t local_us = 0;  // sender's local clock at the same instant
};

// Encoders return the number of bytes written (0 if `cap` is too small).
// Decoders return true and fill the out-param only for a well-formed
// message of the right type; anything else — short packet, bad magic,
// unknown version, wrong type — is rejected.
size_t encode_announce(const Header& h, const AnnounceMsg& m, uint8_t* buf,
                       size_t cap);
size_t encode_bye(const Header& h, uint8_t* buf, size_t cap);
size_t encode_ping(const Header& h, const PingMsg& m, uint8_t* buf, size_t cap);
size_t encode_pong(const Header& h, const PongMsg& m, uint8_t* buf, size_t cap);
size_t encode_tsf_hint(const Header& h, const TsfHintMsg& m, uint8_t* buf,
                       size_t cap);

// Peeks the header of any Neon Sync packet. False for non-Neon-Sync or
// future-version traffic (a v1 node ignores versions it does not speak).
bool decode_header(const uint8_t* data, size_t len, Header& out);

bool decode_announce(const uint8_t* data, size_t len, AnnounceMsg& out);
bool decode_ping(const uint8_t* data, size_t len, PingMsg& out);
bool decode_pong(const uint8_t* data, size_t len, PongMsg& out);
bool decode_tsf_hint(const uint8_t* data, size_t len, TsfHintMsg& out);

}  // namespace nsync
