// httpd_t41.cpp's single-session web server, re-plumbed from QNEthernet
// client polling onto lwIP's raw TCP callbacks. The state machine is
// the same — read header, read body, send head + payload, close — but
// bytes arrive in recv callbacks (fired from usbnet::poll's netif input
// pump, so still main-loop context) and leave through tcp_write against
// tcp_sndbuf backpressure, resumed from the sent callback.

#include "httpd_netlink.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "app_state/audio_bus.h"
#include "app_state/config_store.h"
#include "app_state/timeline_bus.h"
#include "neon/config/json.hpp"
#include "neon/transport.hpp"

#include "pulse_hw_daisy.h"
#include "timebase_daisy.h"
#include "usbnet_daisy.h"

#include "lwip/tcp.h"

// Generated from components/web_ui/www/dist/index.html.gz by
// daisy/netlink/scripts/gen_web_bundle.py (build-time, into gen/).
extern const unsigned char neon_web_index_gz[];
extern const unsigned int neon_web_index_gz_len;

namespace webui {
namespace {

constexpr size_t kHeaderCap = 1024;
constexpr size_t kBodyCap = 16384;
constexpr size_t kJsonCap = 8192;
constexpr int64_t kSessionTimeoutUs = 8 * 1000000ll;

char g_header[kHeaderCap];
char g_body[kBodyCap];
char g_json[kJsonCap];
// Response head + small JSON payloads.
char g_resp_head[512];

bool g_reboot = false;

struct tcp_pcb* g_listener = nullptr;

struct Session {
  struct tcp_pcb* pcb = nullptr;
  enum class State : uint8_t { kIdle, kReadHeader, kReadBody, kSend } state =
      State::kIdle;
  size_t header_len = 0;
  size_t body_len = 0;
  size_t body_want = 0;
  int64_t deadline_us = 0;

  // Send plan: head, then an optional payload from RAM or flash.
  size_t head_len = 0;
  size_t head_sent = 0;
  const uint8_t* payload = nullptr;
  size_t payload_len = 0;
  size_t payload_sent = 0;
};
Session g_s;

// --- request parsing ---------------------------------------------------

bool method_is(const char* m) {
  const size_t n = std::strlen(m);
  return std::strncmp(g_header, m, n) == 0 && g_header[n] == ' ';
}

// Path (NUL-terminated in place on first call per request).
char* path() {
  char* sp = std::strchr(g_header, ' ');
  return sp != nullptr ? sp + 1 : g_header;
}

bool path_is(const char* p) {
  const char* q = path();
  const size_t n = std::strlen(p);
  if (std::strncmp(q, p, n) != 0) {
    return false;
  }
  return q[n] == ' ' || q[n] == '?' || q[n] == '\0';
}

// Query parameter out of the request line; false when absent.
bool query_param(const char* key, char* out, size_t cap) {
  const char* q = std::strchr(path(), '?');
  const char* end = std::strchr(path(), ' ');
  if (q == nullptr || (end != nullptr && q > end)) {
    return false;
  }
  ++q;
  const size_t klen = std::strlen(key);
  while (q != nullptr && *q != '\0' && *q != ' ') {
    if (std::strncmp(q, key, klen) == 0 && q[klen] == '=') {
      const char* v = q + klen + 1;
      size_t n = 0;
      while (v[n] != '\0' && v[n] != '&' && v[n] != ' ' && n + 1 < cap) {
        ++n;
      }
      std::memcpy(out, v, n);
      out[n] = '\0';
      return true;
    }
    q = std::strchr(q, '&');
    if (q != nullptr) {
      ++q;
    }
  }
  return false;
}

size_t content_length() {
  const char* h = std::strstr(g_header, "Content-Length:");
  if (h == nullptr) {
    h = std::strstr(g_header, "content-length:");
  }
  return h != nullptr ? static_cast<size_t>(std::atoi(h + 15)) : 0;
}

// --- responses ---------------------------------------------------------

void pump_send();

void respond(const char* status, const char* content_type,
             const uint8_t* payload, size_t len, const char* extra_hdr) {
  g_s.head_len = static_cast<size_t>(std::snprintf(
      g_resp_head, sizeof(g_resp_head),
      "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %u\r\n"
      "Cache-Control: no-store\r\n%sConnection: close\r\n\r\n",
      status, content_type, static_cast<unsigned>(len),
      extra_hdr != nullptr ? extra_hdr : ""));
  g_s.head_sent = 0;
  g_s.payload = payload;
  g_s.payload_len = len;
  g_s.payload_sent = 0;
  g_s.state = Session::State::kSend;
  pump_send();
}

void respond_json(const char* json) {
  respond("200 OK", "application/json",
          reinterpret_cast<const uint8_t*>(json), std::strlen(json), nullptr);
}

void respond_err(const char* status, const char* msg) {
  static char body[128];
  std::snprintf(body, sizeof(body), "{\"ok\":false,\"error\":\"%s\"}", msg);
  respond(status, "application/json", reinterpret_cast<const uint8_t*>(body),
          std::strlen(body), nullptr);
}

void send_ok() { respond_json("{\"ok\":true}"); }

// --- handlers (the teensy41 REST surface, daisy-flavored) --------------

void handle_index() {
  respond("200 OK", "text/html", neon_web_index_gz, neon_web_index_gz_len,
          "Content-Encoding: gzip\r\n");
}

void handle_get_config() {
  const size_t n = neon::config_to_json(neon_config(), g_json, kJsonCap);
  if (n == 0) {
    respond_err("500 Internal Server Error", "encode failed");
    return;
  }
  respond("200 OK", "application/json",
          reinterpret_cast<const uint8_t*>(g_json), n, nullptr);
}

void handle_put_config() {
  const neon::Config before = neon_config();
  neon::Config cfg = before;
  if (!neon::config_from_json(g_body, g_s.body_len, &cfg)) {
    respond_err("400 Bad Request", "invalid JSON");
    return;
  }
  if (!neon_config_save(cfg)) {
    respond_err("500 Internal Server Error", "persist failed");
    return;
  }
  const neon::Config& after = neon_config();
  if (std::strcmp(before.device_name, after.device_name) != 0) {
    // Renaming moves the editor's address; follow it live (mirrors the
    // ESP server's mdns_set_hostname on rename).
    usbnet::set_hostname(after.device_name);
  }
  handle_get_config();
}

void handle_transport() {
  char op[16] = {};
  if (!query_param("op", op, sizeof(op))) {
    respond_err("400 Bad Request", "op required");
    return;
  }
  ControlCommand cmd{};
  if (std::strcmp(op, "play") == 0) {
    cmd.kind = ControlCommand::Kind::kPlay;
  } else if (std::strcmp(op, "stop") == 0) {
    cmd.kind = ControlCommand::Kind::kStop;
  } else if (std::strcmp(op, "toggle") == 0) {
    cmd.kind = ControlCommand::Kind::kToggle;
  } else if (std::strcmp(op, "play_now") == 0) {
    cmd.kind = ControlCommand::Kind::kPlayNow;
  } else if (std::strcmp(op, "stop_now") == 0) {
    cmd.kind = ControlCommand::Kind::kStopNow;
  } else {
    respond_err("400 Bad Request", "unknown op");
    return;
  }
  if (!control_queue_push(cmd)) {
    respond_err("500 Internal Server Error", "control queue full");
    return;
  }
  send_ok();
}

void handle_tempo() {
  char val[24] = {};
  ControlCommand cmd{};
  if (query_param("bpm", val, sizeof(val))) {
    double bpm = std::atof(val);
    if (!(bpm > 0.0)) {
      bpm = static_cast<double>(neon::kMinMilliBpm) / 1000.0;
    } else if (bpm > static_cast<double>(neon::kMaxMilliBpm) / 1000.0) {
      bpm = static_cast<double>(neon::kMaxMilliBpm) / 1000.0;
    }
    cmd.kind = ControlCommand::Kind::kSetTempo;
    cmd.arg = static_cast<int32_t>(neon::milli_bpm_from_bpm(bpm));
  } else if (query_param("op", val, sizeof(val))) {
    if (std::strcmp(val, "tap") == 0) {
      cmd.kind = ControlCommand::Kind::kTapTempo;
    } else if (std::strcmp(val, "double") == 0) {
      cmd.kind = ControlCommand::Kind::kDoubleTempo;
    } else if (std::strcmp(val, "half") == 0) {
      cmd.kind = ControlCommand::Kind::kHalveTempo;
    } else if (std::strcmp(val, "nudge") == 0) {
      char delta[8] = {};
      cmd.kind = ControlCommand::Kind::kNudgeTempo;
      cmd.arg = query_param("delta", delta, sizeof(delta)) ? std::atoi(delta)
                                                           : 1;
    } else {
      respond_err("400 Bad Request", "unknown op");
      return;
    }
  } else {
    respond_err("400 Bad Request", "bpm or op required");
    return;
  }
  if (!control_queue_push(cmd)) {
    respond_err("500 Internal Server Error", "control queue full");
    return;
  }
  send_ok();
}

void handle_resync() {
  char op[8] = {};
  const bool now =
      query_param("op", op, sizeof(op)) && std::strcmp(op, "now") == 0;
  ControlCommand cmd{};
  cmd.kind = now ? ControlCommand::Kind::kResyncNow
                 : ControlCommand::Kind::kResyncNextLoop;
  if (!control_queue_push(cmd)) {
    respond_err("500 Internal Server Error", "control queue full");
    return;
  }
  send_ok();
}

void handle_preset() {
  char op[16] = {};
  char slot_s[8] = {};
  if (!query_param("op", op, sizeof(op)) ||
      !query_param("slot", slot_s, sizeof(slot_s))) {
    respond_err("400 Bad Request", "op and slot required");
    return;
  }
  const int slot = std::atoi(slot_s);
  bool ok = false;
  if (std::strcmp(op, "save") == 0) {
    ok = neon_preset_save(slot);
  } else if (std::strcmp(op, "recall") == 0) {
    ok = neon_preset_recall(slot);
  }
  if (!ok) {
    respond_err("400 Bad Request", "bad op/slot or empty");
    return;
  }
  send_ok();
}

void handle_status() {
  neon::TimelineSnapshot tl;
  timeline_bus().read(tl);
  const int64_t now = daisy_now_us();
  const uint64_t mpb_us = (tl.tempo_mpb_q32 + (1ull << 31)) >> 32;
  const uint32_t mbpm = mpb_us != 0 ? neon::milli_bpm_from_mpb_us(mpb_us) : 0;
  const uint32_t phase = neon::phase_milli_beats(tl, now);
  const uint32_t quantum = tl.quantum_beats != 0 ? tl.quantum_beats : 4;
  const auto& cfg = neon_config();
  char ip[16] = {};
  usbnet::primary_ip(ip, sizeof(ip));
  neon::AudioStatus audio;
  audio_status_bus().read(audio);
  neon::FollowStatus follow;
  follow_status_bus().read(follow);
  const char* fsrc = "none";
  switch (app_status_follow_source()) {
    case FollowSource::kClk:
      fsrc = "clk";
      break;
    case FollowSource::kMidi:
      fsrc = "midi";
      break;
    case FollowSource::kAudio:
      fsrc = "audio";
      break;
    default:
      break;
  }
  const char* flock = follow.lock == 2   ? "locked"
                      : follow.lock == 1 ? "acquiring"
                                         : "idle";

  const int n = std::snprintf(
      g_json, kJsonCap,
      "{\"bpm\":%u.%03u,\"peers\":%u,\"playing\":%s,\"network\":\"%s\","
      "\"ext_clock\":%s,\"follow_source\":\"%s\",\"uptime_s\":%lld,"
      "\"phase_milli\":%u,\"quantum\":%u,\"tempo_valid\":%s,"
      "\"hostname\":\"%s.local\",\"device_name\":\"%s\",\"ip\":\"%s\","
      "\"setup_ap\":false,\"ap_ssid\":\"\","
      "\"wifi_ssid\":\"\",\"wifi_pass_len\":0,\"wifi_fail_reason\":0,"
      "\"firmware\":\"%s\",\"rev\":%u,\"set_bpm\":%u.%03u,"
      "\"pulse\":{\"edges\":%u,\"late_max_us\":%u,\"late_avg_us\":%u},"
      "\"audio\":{\"running\":%s,\"underruns\":0,\"peak_l\":%u,"
      "\"peak_r\":%u,\"publishing\":false,\"subscribers\":0,"
      "\"sub_state\":\"idle\",\"sub_rate\":0,\"sub_dropped\":0,"
      "\"fill_ms\":0,\"clock_ppm\":0,\"rx_dropped\":0,"
      "\"jit_underruns\":0,\"tx_dropped\":0,\"trim_ppm\":0,"
      "\"concealed\":0,"
      "\"follow\":{\"enabled\":%s,\"lock\":\"%s\",\"subdiv\":%u,"
      "\"bpm\":%u.%03u,\"onset_hz\":%u.%u,\"published_mbpm\":%u,"
      "\"no_adc\":%s},\"follow_inputs\":[\"line\"]}}",
      static_cast<unsigned>(mbpm / 1000), static_cast<unsigned>(mbpm % 1000),
      static_cast<unsigned>(app_status_peers()),
      tl.playing != 0 ? "true" : "false",
      usbnet::has_ip() ? "ethernet" : "none",
      app_status_ext_clock() ? "true" : "false", fsrc,
      static_cast<long long>(now / 1000000),
      static_cast<unsigned>(phase), static_cast<unsigned>(quantum),
      tl.tempo_mpb_q32 != 0 ? "true" : "false", cfg.device_name,
      cfg.device_name, ip, NEON_DAISY_FIRMWARE,
      static_cast<unsigned>(neon_config_rev()),
      static_cast<unsigned>(cfg.tempo_milli_bpm / 1000),
      static_cast<unsigned>(cfg.tempo_milli_bpm % 1000),
      static_cast<unsigned>(PulseHwDaisy::edges()),
      static_cast<unsigned>(PulseHwDaisy::late_max_us()),
      static_cast<unsigned>(PulseHwDaisy::late_avg_us()),
      audio.running != 0 ? "true" : "false",
      static_cast<unsigned>(audio.peak_l),
      static_cast<unsigned>(audio.peak_r),
      follow.enabled != 0 ? "true" : "false", flock,
      static_cast<unsigned>(follow.subdiv),
      static_cast<unsigned>(follow.mbpm / 1000),
      static_cast<unsigned>(follow.mbpm % 1000),
      static_cast<unsigned>(follow.onset_hz_x10 / 10),
      static_cast<unsigned>(follow.onset_hz_x10 % 10),
      static_cast<unsigned>(follow.published_mbpm),
      follow.no_adc != 0 ? "true" : "false");
  if (n < 0) {
    respond_err("500 Internal Server Error", "status encode");
    return;
  }
  respond("200 OK", "application/json",
          reinterpret_cast<const uint8_t*>(g_json),
          static_cast<size_t>(n) < kJsonCap ? static_cast<size_t>(n)
                                            : kJsonCap - 1,
          nullptr);
}

void handle_factory_reset() {
  char confirm[8] = {};
  if (!query_param("confirm", confirm, sizeof(confirm)) ||
      std::strcmp(confirm, "yes") != 0) {
    respond_err("400 Bad Request", "confirm=yes required");
    return;
  }
  const bool ok = neon_config_factory_reset();
  g_reboot = true;
  respond_json(ok ? "{\"ok\":true,\"rebooting\":true}"
                  : "{\"ok\":false,\"rebooting\":true}");
}

void route() {
  if (method_is("GET") && path_is("/")) {
    handle_index();
  } else if (path_is("/api/config")) {
    if (method_is("GET")) {
      handle_get_config();
    } else {
      handle_put_config();
    }
  } else if (path_is("/api/status")) {
    handle_status();
  } else if (path_is("/api/transport")) {
    handle_transport();
  } else if (path_is("/api/tempo")) {
    handle_tempo();
  } else if (path_is("/api/resync")) {
    handle_resync();
  } else if (path_is("/api/preset")) {
    handle_preset();
  } else if (path_is("/api/reboot")) {
    neon_config_flush_now();
    g_reboot = true;
    respond_json("{\"ok\":true,\"rebooting\":true}");
  } else if (path_is("/api/factory_reset")) {
    handle_factory_reset();
  } else if (path_is("/api/scan")) {
    respond_json("[]");  // no WiFi radio on this hardware
  } else if (path_is("/api/audio/channels")) {
    respond_json("{\"available\":false,\"channels\":[]}");
  } else if (path_is("/api/ota")) {
    respond_err("501 Not Implemented",
                "network OTA not supported on daisy; flash over USB DFU");
  } else {
    respond_err("404 Not Found", "no such endpoint");
  }
}

// --- lwIP plumbing -----------------------------------------------------

void detach_pcb(struct tcp_pcb* pcb) {
  tcp_arg(pcb, nullptr);
  tcp_recv(pcb, nullptr);
  tcp_sent(pcb, nullptr);
  tcp_err(pcb, nullptr);
}

// Close (or abort) whatever connection is active and go idle.
void reset_session(bool abort) {
  if (g_s.pcb != nullptr) {
    struct tcp_pcb* pcb = g_s.pcb;
    detach_pcb(pcb);
    if (abort || tcp_close(pcb) != ERR_OK) {
      tcp_abort(pcb);
    }
  }
  g_s = Session{};
}

// Queue as much of the send plan as tcp_sndbuf allows; close when the
// whole response has been handed to the stack (lwIP sends FIN after the
// queued data — "Connection: close" semantics for free).
void pump_send() {
  if (g_s.state != Session::State::kSend || g_s.pcb == nullptr) {
    return;
  }
  struct tcp_pcb* pcb = g_s.pcb;
  while (true) {
    if (g_s.head_sent < g_s.head_len) {
      const size_t want = g_s.head_len - g_s.head_sent;
      size_t chunk = tcp_sndbuf(pcb);
      if (chunk == 0) {
        break;
      }
      if (chunk > want) {
        chunk = want;
      }
      // The head buffer is rewritten per request; copy into the stack.
      if (tcp_write(pcb, g_resp_head + g_s.head_sent, (u16_t)chunk,
                    TCP_WRITE_FLAG_COPY) != ERR_OK) {
        break;
      }
      g_s.head_sent += chunk;
    } else if (g_s.payload_sent < g_s.payload_len) {
      const size_t want = g_s.payload_len - g_s.payload_sent;
      size_t chunk = tcp_sndbuf(pcb);
      if (chunk == 0) {
        break;
      }
      if (chunk > want) {
        chunk = want;
      }
      // Payloads are stable for the life of the response (flash for the
      // web bundle, per-request statics for JSON): no copy, the stack
      // references them directly.
      if (tcp_write(pcb, g_s.payload + g_s.payload_sent, (u16_t)chunk, 0) !=
          ERR_OK) {
        break;
      }
      g_s.payload_sent += chunk;
    } else {
      tcp_output(pcb);
      reset_session(false);
      return;
    }
  }
  tcp_output(pcb);
}

void feed(const char* data, size_t len);

err_t on_recv(void* arg, struct tcp_pcb* tpcb, struct pbuf* p, err_t err) {
  (void)arg;
  if (p == nullptr || err != ERR_OK) {
    // Remote closed (or errored) — nothing more is coming.
    if (p != nullptr) {
      pbuf_free(p);
    }
    if (tpcb == g_s.pcb) {
      reset_session(true);
    }
    return ERR_OK;
  }
  tcp_recved(tpcb, p->tot_len);
  for (struct pbuf* q = p; q != nullptr; q = q->next) {
    feed(static_cast<const char*>(q->payload), q->len);
  }
  pbuf_free(p);
  return ERR_OK;
}

err_t on_sent(void* arg, struct tcp_pcb* tpcb, u16_t len) {
  (void)arg;
  (void)tpcb;
  (void)len;
  pump_send();
  return ERR_OK;
}

void on_err(void* arg, err_t err) {
  (void)arg;
  (void)err;
  // The pcb is already freed by lwIP; just forget it.
  g_s = Session{};
}

err_t on_accept(void* arg, struct tcp_pcb* newpcb, err_t err) {
  (void)arg;
  if (err != ERR_OK || newpcb == nullptr) {
    return ERR_VAL;
  }
  if (g_s.state != Session::State::kIdle) {
    // One request at a time, like the teensy server; the editor's
    // sequential fetches never need more.
    return ERR_MEM;
  }
  g_s.pcb = newpcb;
  g_s.state = Session::State::kReadHeader;
  g_s.header_len = 0;
  g_s.deadline_us = daisy_now_us() + kSessionTimeoutUs;
  tcp_recv(newpcb, on_recv);
  tcp_sent(newpcb, on_sent);
  tcp_err(newpcb, on_err);
  return ERR_OK;
}

// Byte-feed the header/body state machine (recv callback context).
void feed(const char* data, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    switch (g_s.state) {
      case Session::State::kReadHeader: {
        if (g_s.header_len + 1 >= kHeaderCap) {
          respond_err("431 Request Header Fields Too Large", "header too big");
          return;
        }
        g_header[g_s.header_len++] = data[i];
        if (g_s.header_len >= 4 &&
            std::memcmp(g_header + g_s.header_len - 4, "\r\n\r\n", 4) == 0) {
          g_header[g_s.header_len] = '\0';
          g_s.body_want = content_length();
          if (g_s.body_want > kBodyCap - 1) {
            respond_err("400 Bad Request", "body too large");
            return;
          }
          g_s.body_len = 0;
          if (g_s.body_want > 0) {
            g_s.state = Session::State::kReadBody;
          } else {
            route();
            if (g_s.state != Session::State::kReadHeader) {
              return;  // response queued (or session reset)
            }
          }
        }
        break;
      }
      case Session::State::kReadBody: {
        g_body[g_s.body_len++] = data[i];
        if (g_s.body_len >= g_s.body_want) {
          g_body[g_s.body_len] = '\0';
          route();
          return;
        }
        break;
      }
      default:
        return;  // already answering; ignore pipelined extra bytes
    }
  }
}

}  // namespace

void init() {
  struct tcp_pcb* pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
  if (pcb == nullptr) {
    return;
  }
  if (tcp_bind(pcb, IP_ANY_TYPE, 80) != ERR_OK) {
    tcp_abort(pcb);
    return;
  }
  g_listener = tcp_listen_with_backlog(pcb, 2);
  if (g_listener != nullptr) {
    tcp_accept(g_listener, on_accept);
  }
}

void poll(int64_t now_us) {
  // Data moves in lwIP callbacks; the poll's job is the wall clock.
  if (g_s.state != Session::State::kIdle && now_us > g_s.deadline_us) {
    reset_session(true);
  }
}

bool reboot_requested() { return g_reboot; }

}  // namespace webui
