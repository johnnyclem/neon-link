// OSC over UDP (docs/OSC.md, docs/SOLAROS_PORTS_HANDOFF.md §4).
// Inbound /neon/* control lands on the same ControlCommand funnel as
// the web editor and the panels, with the same tempo clamps; outbound
// is a fixed binding set (tempo, playing, beat, peers) with the
// delta-vs-last-sent / edge / two-phase-send policy from
// neon::osc::OutBinding. One socket, one task, off by default —
// cfg.osc_enabled gates an unauthenticated LAN surface.

#include "sdkconfig.h"
#include "tasks.h"

#include "app_state/config_store.h"
#include "app_state/timeline_bus.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "neon/config/model.hpp"
#include "neon/fixed_math.hpp"
#include "neon/osc/bindings.hpp"
#include "neon/osc/codec.hpp"
#include "neon/osc/scene.hpp"
#include "neon/timeline.hpp"
#include "neon/transport.hpp"

#include "lwip/ip4_addr.h"
#include "lwip/sockets.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

const char* kTag = "osc";

constexpr int kInboundPerSecondMax = 100;

int g_sock = -1;
uint16_t g_bound_port = 0;
sockaddr_in g_target{};
bool g_target_ok = false;
char g_target_str[sizeof(neon::Config{}.osc_target)] = {};

// Fixed outbound bindings.
neon::osc::OutBinding g_tempo;
neon::osc::OutBinding g_playing;
neon::osc::OutBinding g_beat;
neon::osc::OutBinding g_peers;

// Inbound flood control: 1 s tumbling window.
int64_t g_window_us = 0;
int g_window_packets = 0;
uint32_t g_rate_limited = 0;
std::atomic<int> g_scene_next{0};
std::atomic<int> g_scene_prev{0};
std::atomic<int> g_scene_selected{-1};
std::atomic<int> g_scene_count{-1};

int g_live_sock = -1;
int64_t g_live_query_us = 0;
bool g_live_listening = false;

constexpr char kAbletonSceneNext[] = "/live/scene/fire_selected";
constexpr char kAbletonSceneFire[] = "/live/scene/fire";
constexpr char kAbletonStopClips[] = "/live/song/stop_all_clips";
constexpr char kAbletonGetSelected[] = "/live/view/get/selected_scene";
constexpr char kAbletonListenSelected[] = "/live/view/start_listen/selected_scene";
constexpr char kAbletonGetCount[] = "/live/song/get/num_scenes";
constexpr uint16_t kAbletonReplyPort = 11001;

int live_dispatch(void*, const char* address, float value) {
  if (std::strcmp(address, "/live/view/get/selected_scene") == 0) {
    g_scene_selected.store(static_cast<int>(value), std::memory_order_relaxed);
    return 1;
  }
  if (std::strcmp(address, "/live/song/get/num_scenes") == 0) {
    g_scene_count.store(static_cast<int>(value), std::memory_order_relaxed);
    return 1;
  }
  return 0;
}

int dispatch(void*, const char* address, float value) {
  ControlCommand cmd{};
  if (std::strcmp(address, "/neon/tempo") == 0) {
    // Same clamp discipline as the web editor's ?bpm=: stay in double
    // space until the range check is done (the codec already rejected
    // NaN/inf, but a hostile float can still be out of range).
    double bpm = static_cast<double>(value);
    if (!(bpm > 0.0)) {
      return -1;
    }
    const double max_bpm = static_cast<double>(neon::kMaxMilliBpm) / 1000.0;
    const double min_bpm = static_cast<double>(neon::kMinMilliBpm) / 1000.0;
    if (bpm < min_bpm) {
      bpm = min_bpm;
    } else if (bpm > max_bpm) {
      bpm = max_bpm;
    }
    cmd.kind = ControlCommand::Kind::kSetTempo;
    cmd.arg = static_cast<int32_t>(neon::milli_bpm_from_bpm(bpm));
  } else if (std::strcmp(address, "/neon/nudge") == 0) {
    const float r = std::round(value);
    if (r < -50.0f || r > 50.0f || r == 0.0f) {
      return -1;
    }
    cmd.kind = ControlCommand::Kind::kNudgeTempo;
    cmd.arg = static_cast<int32_t>(r);
  } else if (std::strcmp(address, "/neon/transport") == 0) {
    cmd.kind = value != 0.0f ? ControlCommand::Kind::kPlay
                             : ControlCommand::Kind::kStop;
  } else if (std::strcmp(address, "/neon/toggle") == 0) {
    cmd.kind = ControlCommand::Kind::kToggle;
  } else if (std::strcmp(address, "/neon/tap") == 0) {
    cmd.kind = ControlCommand::Kind::kTapTempo;
  } else if (std::strcmp(address, "/neon/resync") == 0) {
    cmd.kind = value != 0.0f ? ControlCommand::Kind::kResyncNow
                             : ControlCommand::Kind::kResyncNextLoop;
  } else {
    return 0;
  }
  control_queue_push(cmd);
  return 1;
}

void close_socket() {
  if (g_sock >= 0) {
    lwip_close(g_sock);
    g_sock = -1;
    g_bound_port = 0;
    ESP_LOGI(kTag, "closed");
  }
}

bool open_socket(uint16_t port) {
  close_socket();
  const int s = lwip_socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
  if (s < 0) {
    return false;
  }
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  if (lwip_bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    lwip_close(s);
    return false;
  }
  g_sock = s;
  g_bound_port = port;
  if (port == 0) {
    ESP_LOGI(kTag, "send-only UDP (ephemeral)");
  } else {
    ESP_LOGI(kTag, "listening on udp/%u", static_cast<unsigned>(port));
  }
  return true;
}

// "host:port" → sockaddr. IPv4 literals only — a tempo box does not
// carry a resolver dependency for its telemetry side channel.
bool parse_target(const char* target, sockaddr_in* out) {
  char host[sizeof(g_target_str)] = {};
  std::snprintf(host, sizeof(host), "%s", target);
  char* colon = std::strrchr(host, ':');
  long port = 9000;
  if (colon != nullptr) {
    *colon = '\0';
    port = std::strtol(colon + 1, nullptr, 10);
  }
  if (host[0] == '\0' || port <= 0 || port > 65535) {
    return false;
  }
  ip4_addr_t ip{};
  if (!ip4addr_aton(host, &ip)) {
    return false;
  }
  std::memset(out, 0, sizeof(*out));
  out->sin_family = AF_INET;
  out->sin_port = htons(static_cast<uint16_t>(port));
  out->sin_addr.s_addr = ip.addr;
  return true;
}

void send_binding(neon::osc::OutBinding& b, const char* address, bool as_int) {
  uint8_t pkt[96];
  const float v = b.pending();
  const size_t n =
      as_int ? neon::osc::encode_int(address, static_cast<int32_t>(v), pkt,
                                     sizeof(pkt))
             : neon::osc::encode_float(address, v, pkt, sizeof(pkt));
  if (n == 0) {
    return;
  }
  const int sent =
      lwip_sendto(g_sock, pkt, n, 0, reinterpret_cast<sockaddr*>(&g_target),
                  sizeof(g_target));
  if (sent == static_cast<int>(n)) {
    b.note_sent();
  }
}

void close_live_socket() {
  if (g_live_sock >= 0) {
    lwip_close(g_live_sock);
    g_live_sock = -1;
    g_live_listening = false;
  }
}

bool open_live_socket() {
  if (g_live_sock >= 0) {
    return true;
  }
  const int s = lwip_socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
  if (s < 0) {
    return false;
  }
  int yes = 1;
  lwip_setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(kAbletonReplyPort);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  if (lwip_bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    lwip_close(s);
    return false;
  }
  g_live_sock = s;
  ESP_LOGI(kTag, "AbletonOSC reply udp/%u", kAbletonReplyPort);
  return true;
}

int live_send(const uint8_t* pkt, size_t n) {
  if (g_live_sock < 0 || !g_target_ok) {
    return -1;
  }
  return lwip_sendto(g_live_sock, pkt, n, 0,
                     reinterpret_cast<sockaddr*>(&g_target), sizeof(g_target));
}

void live_bang(const char* address) {
  uint8_t pkt[96];
  const size_t n = neon::osc::encode_bang(address, pkt, sizeof(pkt));
  if (n == 0) {
    return;
  }
  live_send(pkt, n);
}

void live_int(const char* address, int32_t v) {
  uint8_t pkt[96];
  const size_t n = neon::osc::encode_int(address, v, pkt, sizeof(pkt));
  if (n == 0) {
    return;
  }
  live_send(pkt, n);
}

void live_stop() {
  live_bang(kAbletonStopClips);
  ControlCommand cmd{};
  cmd.kind = ControlCommand::Kind::kStopNow;
  control_queue_push(cmd);
  ESP_LOGI(kTag, "scene stop");
}

void live_query(int64_t now) {
  live_bang(kAbletonGetSelected);
  live_bang(kAbletonGetCount);
  if (!g_live_listening) {
    live_bang(kAbletonListenSelected);
    g_live_listening = true;
  }
  g_live_query_us = now;
}

void drain_live() {
  if (g_live_sock < 0) {
    return;
  }
  for (int i = 0; i < 8; ++i) {
    uint8_t pkt[neon::osc::kMaxPacket];
    sockaddr_in from{};
    socklen_t from_len = sizeof(from);
    const int n =
        lwip_recvfrom(g_live_sock, pkt, sizeof(pkt), MSG_DONTWAIT,
                      reinterpret_cast<sockaddr*>(&from), &from_len);
    if (n <= 0) {
      return;
    }
    neon::osc::DispatchStats st;
    neon::osc::parse_packet(pkt, static_cast<size_t>(n), live_dispatch,
                            nullptr, &st);
  }
}

void apply_scene(const neon::osc::SceneDecision& d) {
  switch (d.op) {
    case neon::osc::SceneOp::kStop:
      live_stop();
      break;
    case neon::osc::SceneOp::kFireIndex: {
      live_int(kAbletonSceneFire, d.index);
      g_scene_selected.store(d.index, std::memory_order_relaxed);
      ControlCommand play{};
      play.kind = ControlCommand::Kind::kPlayNow;
      control_queue_push(play);
      ESP_LOGI(kTag, "scene fire %d", d.index);
      break;
    }
    case neon::osc::SceneOp::kFireSelected:
    default: {
      live_bang(kAbletonSceneNext);
      ControlCommand play{};
      play.kind = ControlCommand::Kind::kPlayNow;
      control_queue_push(play);
      ESP_LOGI(kTag, "scene next -> %s", g_target_str);
      break;
    }
  }
}

void send_scene_bangs(int64_t now) {
  if (!g_target_ok) {
    g_scene_next.store(0, std::memory_order_relaxed);
    g_scene_prev.store(0, std::memory_order_relaxed);
    return;
  }
  if (!open_live_socket()) {
    return;
  }
  drain_live();
  if (now - g_live_query_us > 2000000) {
    live_query(now);
  }
  int next = g_scene_next.exchange(0, std::memory_order_relaxed);
  int prev = g_scene_prev.exchange(0, std::memory_order_relaxed);
  if (next > 8) {
    next = 8;
  }
  if (prev > 8) {
    prev = 8;
  }
  int selected = g_scene_selected.load(std::memory_order_relaxed);
  const int count = g_scene_count.load(std::memory_order_relaxed);
  if ((next > 0 || prev > 0) && (selected < 0 || count < 0)) {
    live_query(now);
  }
  if (prev > 0 && selected < 0) {
    prev = 0;
  }
  while (next-- > 0) {
    const auto d = neon::osc::scene_next(selected, count);
    apply_scene(d);
    if (d.op == neon::osc::SceneOp::kStop) {
      // Park one past the last row so a following prev re-fires it.
      if (count > 0 && selected >= count - 1) {
        selected = count;
      }
      break;
    }
    if (selected >= 0) {
      ++selected;
    }
  }
  while (prev-- > 0) {
    const auto d = neon::osc::scene_prev(selected, count);
    apply_scene(d);
    if (d.op == neon::osc::SceneOp::kStop) {
      break;
    }
    selected = d.index;
  }
  if (selected >= 0) {
    g_scene_selected.store(selected, std::memory_order_relaxed);
  }
}

void sample_outbound(int64_t now) {
  if (!g_target_ok || g_sock < 0) {
    return;
  }
  neon::TimelineSnapshot tl{};
  timeline_bus().read(tl);
  const uint64_t mpb_us = (tl.tempo_mpb_q32 + (1ull << 31)) >> 32;
  const uint32_t milli_bpm =
      mpb_us != 0 ? neon::milli_bpm_from_mpb_us(mpb_us) : 120000u;
  const bool playing = tl.playing != 0;
  const uint32_t quantum = tl.quantum_beats != 0 ? tl.quantum_beats : 4;
  const uint32_t beat =
      playing ? neon::beat_number(neon::phase_milli_beats(tl, now), quantum)
              : 0;

  if (g_tempo.due(now) &&
      g_tempo.prepare(now, static_cast<float>(milli_bpm) / 1000.0f)) {
    send_binding(g_tempo, "/neon/tempo", /*as_int=*/false);
  }
  if (g_playing.due(now) && g_playing.prepare(now, playing ? 1.0f : 0.0f)) {
    send_binding(g_playing, "/neon/playing", /*as_int=*/true);
  }
  if (g_beat.due(now) && g_beat.prepare(now, static_cast<float>(beat))) {
    send_binding(g_beat, "/neon/beat", /*as_int=*/true);
  }
  if (g_peers.due(now) &&
      g_peers.prepare(now, static_cast<float>(tl.num_peers))) {
    send_binding(g_peers, "/neon/peers", /*as_int=*/true);
  }
}

void drain_inbound(int64_t now) {
  if (g_sock < 0) {
    return;
  }
  for (int i = 0; i < 4; ++i) {
    uint8_t pkt[neon::osc::kMaxPacket];
    sockaddr_in from{};
    socklen_t from_len = sizeof(from);
    const int n =
        lwip_recvfrom(g_sock, pkt, sizeof(pkt), MSG_DONTWAIT,
                      reinterpret_cast<sockaddr*>(&from), &from_len);
    if (n <= 0) {
      return;
    }
    if (now - g_window_us >= 1000000) {
      g_window_us = now;
      g_window_packets = 0;
    }
    if (++g_window_packets > kInboundPerSecondMax) {
      ++g_rate_limited;
      continue;
    }
    neon::osc::DispatchStats st;
    if (!neon::osc::parse_packet(pkt, static_cast<size_t>(n), dispatch,
                                 nullptr, &st)) {
      ESP_LOGD(kTag, "malformed packet (%d bytes)", n);
    } else if (st.applied > 0) {
      ESP_LOGD(kTag, "applied %d", st.applied);
    }
  }
}

void osc_task(void*) {
  // Beat events want to land close to the beat; everything else is
  // ambient. 250 ms tempo cadence, 0.05 BPM delta.
  g_tempo.configure(neon::osc::OutBinding::Kind::kScalar, 250, 0.05f);
  g_playing.configure(neon::osc::OutBinding::Kind::kEvent, 20, 0.0f);
  g_beat.configure(neon::osc::OutBinding::Kind::kScalar, 20, 0.5f);
  g_peers.configure(neon::osc::OutBinding::Kind::kScalar, 1000, 0.5f);

  for (;;) {
    const neon::Config& cfg = neon_config();
    const bool enabled = cfg.osc_enabled != 0;
    const bool have_target = cfg.osc_target[0] != '\0';
    // Inbound stays gated on osc_enabled. Outbound scene bangs only need
    // a target (AbletonOSC :11000) — no extra listen port on the box.
    if (!enabled && !have_target) {
      close_socket();
      close_live_socket();
      g_target_ok = false;
      g_target_str[0] = '\0';
      g_scene_next.store(0, std::memory_order_relaxed);
      g_scene_prev.store(0, std::memory_order_relaxed);
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }
    const uint16_t bind_port = enabled ? cfg.osc_port : 0;
    if (g_sock < 0 || g_bound_port != bind_port) {
      if (!open_socket(bind_port)) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        continue;
      }
    }
    if (std::strncmp(g_target_str, cfg.osc_target, sizeof(g_target_str)) !=
        0) {
      std::snprintf(g_target_str, sizeof(g_target_str), "%s",
                    cfg.osc_target);
      g_target_ok = parse_target(g_target_str, &g_target);
      if (g_target_str[0] != '\0' && !g_target_ok) {
        ESP_LOGW(kTag, "bad target '%s' (want IPv4 host:port)",
                 g_target_str);
      }
    }

    const int64_t now = esp_timer_get_time();
    if (enabled) {
      drain_inbound(now);
      sample_outbound(now);
    }
    send_scene_bangs(now);
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

}  // namespace

void neon_start_osc_service() {
  // Priority 2: below every timing-critical task; the queue push into
  // the Link service is the only cross-task interaction.
  xTaskCreatePinnedToCore(osc_task, "osc", 4096, nullptr, 2, nullptr, 0);
}

void neon_osc_scene_next() {
  g_scene_next.fetch_add(1, std::memory_order_relaxed);
}

void neon_osc_scene_prev() {
  g_scene_prev.fetch_add(1, std::memory_order_relaxed);
}

int neon_osc_scene_index() {
  return g_scene_selected.load(std::memory_order_relaxed);
}

int neon_osc_scene_count() {
  return g_scene_count.load(std::memory_order_relaxed);
}
