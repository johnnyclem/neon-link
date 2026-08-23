#include "nsync/wire.hpp"

#include <cstring>

namespace nsync {

namespace {

// Little-endian cursor writer/reader. The reader tracks an `ok` flag
// instead of returning per-field results so decode bodies stay linear.
class Writer {
 public:
  Writer(uint8_t* buf, size_t cap) : buf_(buf), cap_(cap) {}

  void u8(uint8_t v) { byte(v); }
  void u16(uint16_t v) {
    byte(static_cast<uint8_t>(v));
    byte(static_cast<uint8_t>(v >> 8));
  }
  void u32(uint32_t v) {
    u16(static_cast<uint16_t>(v));
    u16(static_cast<uint16_t>(v >> 16));
  }
  void u64(uint64_t v) {
    u32(static_cast<uint32_t>(v));
    u32(static_cast<uint32_t>(v >> 32));
  }
  void i64(int64_t v) { u64(static_cast<uint64_t>(v)); }
  void bytes(const uint8_t* p, size_t n) {
    for (size_t i = 0; i < n; ++i) {
      byte(p[i]);
    }
  }

  // 0 on overflow — the caller treats that as "buffer too small".
  size_t written() const { return overflow_ ? 0 : pos_; }

 private:
  void byte(uint8_t v) {
    if (pos_ >= cap_) {
      overflow_ = true;
      return;
    }
    buf_[pos_++] = v;
  }

  uint8_t* buf_;
  size_t cap_;
  size_t pos_ = 0;
  bool overflow_ = false;
};

class Reader {
 public:
  Reader(const uint8_t* buf, size_t len) : buf_(buf), len_(len) {}

  uint8_t u8() { return byte(); }
  uint16_t u16() {
    const uint16_t lo = byte();
    const uint16_t hi = byte();
    return static_cast<uint16_t>(lo | (hi << 8));
  }
  uint32_t u32() {
    const uint32_t lo = u16();
    const uint32_t hi = u16();
    return lo | (hi << 16);
  }
  uint64_t u64() {
    const uint64_t lo = u32();
    const uint64_t hi = u32();
    return lo | (hi << 32);
  }
  int64_t i64() { return static_cast<int64_t>(u64()); }
  void bytes(uint8_t* p, size_t n) {
    for (size_t i = 0; i < n; ++i) {
      p[i] = byte();
    }
  }

  bool ok() const { return !underrun_; }

 private:
  uint8_t byte() {
    if (pos_ >= len_) {
      underrun_ = true;
      return 0;
    }
    return buf_[pos_++];
  }

  const uint8_t* buf_;
  size_t len_;
  size_t pos_ = 0;
  bool underrun_ = false;
};

void put_header(Writer& w, const Header& h, MsgType type) {
  w.bytes(kMagic, sizeof(kMagic));
  w.u8(h.proto_ver);
  w.u8(static_cast<uint8_t>(type));
  w.u16(h.flags);
  w.u64(h.node_id);
  w.u64(h.session_id);
}

bool get_header(Reader& r, Header& h) {
  uint8_t magic[4];
  r.bytes(magic, sizeof(magic));
  h.proto_ver = r.u8();
  h.type = static_cast<MsgType>(r.u8());
  h.flags = r.u16();
  h.node_id = r.u64();
  h.session_id = r.u64();
  if (!r.ok() || std::memcmp(magic, kMagic, sizeof(kMagic)) != 0) {
    return false;
  }
  return h.proto_ver == kProtoVersion;
}

void put_state(Writer& w, const SessionState& s) {
  w.u64(s.tl.seq);
  w.u64(s.tl.writer);
  w.u64(s.tl.origin_session_us);
  w.i64(s.tl.beat_at_origin_q32);
  w.u64(s.tl.tempo_mpb_q32);
  w.u32(s.tl.quantum_mb);
  w.u64(s.tr.seq);
  w.u64(s.tr.writer);
  w.u64(s.tr.toggle_session_us);
  w.u8(s.tr.playing);
}

void get_state(Reader& r, SessionState& s) {
  s.tl.seq = r.u64();
  s.tl.writer = r.u64();
  s.tl.origin_session_us = r.u64();
  s.tl.beat_at_origin_q32 = r.i64();
  s.tl.tempo_mpb_q32 = r.u64();
  s.tl.quantum_mb = r.u32();
  s.tr.seq = r.u64();
  s.tr.writer = r.u64();
  s.tr.toggle_session_us = r.u64();
  s.tr.playing = r.u8();
}

// Header check + type check shared by every decoder.
bool open(const uint8_t* data, size_t len, MsgType want, Reader& r) {
  Header h;
  if (!get_header(r, h) || h.type != want) {
    return false;
  }
  (void)data;
  (void)len;
  return true;
}

}  // namespace

size_t encode_announce(const Header& h, const AnnounceMsg& m, uint8_t* buf,
                       size_t cap) {
  Writer w(buf, cap);
  put_header(w, h, MsgType::kAnnounce);
  put_state(w, m.state);
  w.i64(m.ghost_offset_us);
  w.u64(m.sender_local_tx_us);
  w.u32(m.ttl_ms);
  return w.written();
}

size_t encode_bye(const Header& h, uint8_t* buf, size_t cap) {
  Writer w(buf, cap);
  put_header(w, h, MsgType::kBye);
  return w.written();
}

size_t encode_ping(const Header& h, const PingMsg& m, uint8_t* buf,
                   size_t cap) {
  Writer w(buf, cap);
  put_header(w, h, MsgType::kPing);
  w.u64(m.t1_local_us);
  return w.written();
}

size_t encode_pong(const Header& h, const PongMsg& m, uint8_t* buf,
                   size_t cap) {
  Writer w(buf, cap);
  put_header(w, h, MsgType::kPong);
  w.u64(m.t1_echo_us);
  w.u64(m.t2_remote_rx_us);
  w.u64(m.t3_remote_tx_us);
  return w.written();
}

size_t encode_tsf_hint(const Header& h, const TsfHintMsg& m, uint8_t* buf,
                       size_t cap) {
  Writer w(buf, cap);
  put_header(w, h, MsgType::kTsfHint);
  w.bytes(m.bssid, sizeof(m.bssid));
  w.u64(m.tsf_us);
  w.u64(m.local_us);
  return w.written();
}

bool decode_header(const uint8_t* data, size_t len, Header& out) {
  Reader r(data, len);
  return get_header(r, out) && r.ok();
}

bool decode_announce(const uint8_t* data, size_t len, AnnounceMsg& out) {
  Reader r(data, len);
  if (!open(data, len, MsgType::kAnnounce, r)) {
    return false;
  }
  get_state(r, out.state);
  out.ghost_offset_us = r.i64();
  out.sender_local_tx_us = r.u64();
  out.ttl_ms = r.u32();
  return r.ok();
}

bool decode_ping(const uint8_t* data, size_t len, PingMsg& out) {
  Reader r(data, len);
  if (!open(data, len, MsgType::kPing, r)) {
    return false;
  }
  out.t1_local_us = r.u64();
  return r.ok();
}

bool decode_pong(const uint8_t* data, size_t len, PongMsg& out) {
  Reader r(data, len);
  if (!open(data, len, MsgType::kPong, r)) {
    return false;
  }
  out.t1_echo_us = r.u64();
  out.t2_remote_rx_us = r.u64();
  out.t3_remote_tx_us = r.u64();
  return r.ok();
}

bool decode_tsf_hint(const uint8_t* data, size_t len, TsfHintMsg& out) {
  Reader r(data, len);
  if (!open(data, len, MsgType::kTsfHint, r)) {
    return false;
  }
  r.bytes(out.bssid, sizeof(out.bssid));
  out.tsf_us = r.u64();
  out.local_us = r.u64();
  return r.ok();
}

}  // namespace nsync
