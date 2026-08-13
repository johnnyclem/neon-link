#include "webui/web_ui.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "esp_app_desc.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_state/audio_bus.h"
#include "app_state/config_store.h"
#include "app_state/timeline_bus.h"
#include "halesp/pulse_hw_gptimer.hpp"
#include "neon/config/json.hpp"
#include "neon/net/preference.hpp"
#include "neon/transport.hpp"
#include "netman/net_manager.h"

// Provided by main/wifi.cpp (linked into the final app image).
extern "C" void neon_wifi_apply_credentials(void);
extern "C" uint8_t neon_wifi_last_disconnect_reason(void);
extern "C" const char* neon_wifi_current_ssid(void);
extern "C" int neon_wifi_scan_json(char* buf, int cap);
// Provided by main/audio_service.cpp.
extern "C" int neon_audio_channels_json(char* buf, int cap);

namespace {

const char* kTag = "web_ui";

const char* firmware_version() {
  const esp_app_desc_t* desc = esp_app_get_description();
  return desc != nullptr ? desc->version : "unknown";
}

// Read one query parameter; returns false when absent.
bool query_param(httpd_req_t* req, const char* key, char* out, size_t cap) {
  char query[128] = {};
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
    return false;
  }
  return httpd_query_key_value(query, key, out, cap) == ESP_OK;
}

esp_err_t send_json(httpd_req_t* req, const char* json) {
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

esp_err_t send_ok(httpd_req_t* req) { return send_json(req, "{\"ok\":true}"); }

// SSIDs are whatever the user (or a neighbour) named their network. A
// stray quote or backslash would break the status document and take the
// whole editor offline, so escape before interpolating.
void json_escape(const char* in, char* out, size_t cap) {
  size_t n = 0;
  for (const char* p = in; p != nullptr && *p != '\0' && n + 2 < cap; ++p) {
    const unsigned char c = static_cast<unsigned char>(*p);
    if (c < 0x20) {
      continue;  // control characters have no place in an SSID
    }
    if (c == '"' || c == '\\') {
      out[n++] = '\\';
    }
    out[n++] = *p;
  }
  if (cap != 0) {
    out[n < cap ? n : cap - 1] = '\0';
  }
}

// The single-file web app, pre-compressed at build time (see web/). Served
// straight out of flash with Content-Encoding: gzip — no decompression on
// the module, and roughly a third of the bytes over a 2.4 GHz link the user
// is very likely standing on mid-setup.
extern "C" {
extern const uint8_t index_gz_start[] asm("_binary_index_html_gz_start");
extern const uint8_t index_gz_end[] asm("_binary_index_html_gz_end");
}

esp_err_t handle_index(httpd_req_t* req) {
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  // EMBED_FILES is byte-exact (unlike EMBED_TXTFILES, which appends a NUL),
  // so the whole range is payload.
  const size_t len = static_cast<size_t>(index_gz_end - index_gz_start);
  return httpd_resp_send(req, reinterpret_cast<const char*>(index_gz_start),
                         len);
}

constexpr size_t kConfigJsonCap = 8192;

esp_err_t handle_get_config(httpd_req_t* req) {
  char* buf = static_cast<char*>(std::malloc(kConfigJsonCap));
  if (buf == nullptr) {
    return httpd_resp_send_500(req);
  }
  const size_t n = neon::config_to_json(neon_config(), buf, kConfigJsonCap);
  httpd_resp_set_type(req, "application/json");
  const esp_err_t err =
      n != 0 ? httpd_resp_send(req, buf, n) : httpd_resp_send_500(req);
  std::free(buf);
  return err;
}

esp_err_t handle_put_config(httpd_req_t* req) {
  if (req->content_len == 0 || req->content_len > 16384) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad length");
    return ESP_OK;
  }
  char* body = static_cast<char*>(std::malloc(req->content_len + 1));
  if (body == nullptr) {
    return httpd_resp_send_500(req);
  }
  size_t got = 0;
  while (got < static_cast<size_t>(req->content_len)) {
    const int r = httpd_req_recv(req, body + got, req->content_len - got);
    if (r <= 0) {
      std::free(body);
      return httpd_resp_send_500(req);
    }
    got += static_cast<size_t>(r);
  }
  body[got] = '\0';

  neon::Config cfg = neon_config();
  const bool ok = neon::config_from_json(body, got, &cfg);
  std::free(body);
  if (!ok) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid JSON");
    return ESP_OK;
  }
  // Immediate NVS write — debounced apply alone lost WiFi on quick REBOOT.
  if (!neon_config_save(cfg)) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "NVS save failed");
    return ESP_OK;
  }
  neon_wifi_apply_credentials();
  // Renaming the module moves the editor's address; follow it live so the
  // user is not stranded on a stale .local URL until the next reboot.
  netman::mdns_set_hostname(neon_config().device_name);
  ESP_LOGI(kTag, "config updated from web editor (persisted)");
  return handle_get_config(req);  // respond with the sanitized result
}

// POST /api/transport?op=play|stop|toggle|play_now|stop_now
esp_err_t handle_transport(httpd_req_t* req) {
  char op[16] = {};
  if (!query_param(req, "op", op, sizeof(op))) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "op required");
    return ESP_OK;
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
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "unknown op");
    return ESP_OK;
  }
  if (!control_queue_push(cmd)) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                        "control queue full");
    return ESP_OK;
  }
  return send_ok(req);
}

// POST /api/tempo?bpm=124.5  or  ?op=tap|double|half|nudge&delta=-1
esp_err_t handle_tempo(httpd_req_t* req) {
  char val[24] = {};
  ControlCommand cmd{};
  if (query_param(req, "bpm", val, sizeof(val))) {
    // Clamp in double space first: casting an infinity or NaN to int64 is
    // undefined, and the query string is whatever the client sent.
    double bpm = std::atof(val);
    if (!(bpm > 0.0)) {  // false for NaN and for anything <= 0
      bpm = static_cast<double>(neon::kMinMilliBpm) / 1000.0;
    } else if (bpm > static_cast<double>(neon::kMaxMilliBpm) / 1000.0) {
      bpm = static_cast<double>(neon::kMaxMilliBpm) / 1000.0;
    }
    cmd.kind = ControlCommand::Kind::kSetTempo;
    cmd.arg = static_cast<int32_t>(
        neon::clamp_milli_bpm(static_cast<int64_t>(bpm * 1000.0)));
  } else if (query_param(req, "op", val, sizeof(val))) {
    if (std::strcmp(val, "tap") == 0) {
      cmd.kind = ControlCommand::Kind::kTapTempo;
    } else if (std::strcmp(val, "double") == 0) {
      cmd.kind = ControlCommand::Kind::kDoubleTempo;
    } else if (std::strcmp(val, "half") == 0) {
      cmd.kind = ControlCommand::Kind::kHalveTempo;
    } else if (std::strcmp(val, "nudge") == 0) {
      char delta[8] = {};
      cmd.kind = ControlCommand::Kind::kNudgeTempo;
      cmd.arg = query_param(req, "delta", delta, sizeof(delta))
                    ? std::atoi(delta)
                    : 1;
    } else {
      httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "unknown op");
      return ESP_OK;
    }
  } else {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bpm or op required");
    return ESP_OK;
  }
  if (!control_queue_push(cmd)) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                        "control queue full");
    return ESP_OK;
  }
  return send_ok(req);
}

// POST /api/resync?op=next|now — the legacy "Tap + Play" shift action.
esp_err_t handle_resync(httpd_req_t* req) {
  char op[8] = {};
  const bool now =
      query_param(req, "op", op, sizeof(op)) && std::strcmp(op, "now") == 0;
  ControlCommand cmd{};
  cmd.kind = now ? ControlCommand::Kind::kResyncNow
                 : ControlCommand::Kind::kResyncNextLoop;
  if (!control_queue_push(cmd)) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                        "control queue full");
    return ESP_OK;
  }
  return send_ok(req);
}

// GET /api/scan — nearby 2.4 GHz networks, so the editor can offer a
// pick-list. Blocking: a full scan takes a couple of seconds.
esp_err_t handle_scan(httpd_req_t* req) {
  constexpr size_t kCap = 2048;
  char* buf = static_cast<char*>(std::malloc(kCap));
  if (buf == nullptr) {
    return httpd_resp_send_500(req);
  }
  const int n = neon_wifi_scan_json(buf, static_cast<int>(kCap));
  httpd_resp_set_type(req, "application/json");
  const esp_err_t err = n > 0 ? httpd_resp_send(req, buf, n)
                              : httpd_resp_send(req, "[]", 2);
  std::free(buf);
  return err;
}

void restart_task(void*) {
  vTaskDelay(pdMS_TO_TICKS(600));
  esp_restart();
}

// POST /api/factory_reset?confirm=yes — wipe config and presets, reboot.
esp_err_t handle_factory_reset(httpd_req_t* req) {
  char confirm[8] = {};
  if (!query_param(req, "confirm", confirm, sizeof(confirm)) ||
      std::strcmp(confirm, "yes") != 0) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "confirm=yes required");
    return ESP_OK;
  }
  const bool ok = neon_config_factory_reset();
  ESP_LOGW(kTag, "factory reset requested from web editor");
  send_json(req, ok ? "{\"ok\":true,\"rebooting\":true}"
                    : "{\"ok\":false,\"rebooting\":true}");
  xTaskCreate(restart_task, "reset_reboot", 2048, nullptr, 5, nullptr);
  return ESP_OK;
}

// POST /api/ota — raw firmware image in the body. Streams straight into
// the inactive app slot; the module reboots into it on success.
esp_err_t handle_ota(httpd_req_t* req) {
  const esp_partition_t* target = esp_ota_get_next_update_partition(nullptr);
  if (target == nullptr) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                        "no OTA partition (reflash with the new table)");
    return ESP_OK;
  }
  if (req->content_len == 0) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty body");
    return ESP_OK;
  }
  const size_t total = static_cast<size_t>(req->content_len);
  if (total > target->size) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                        "image larger than the app partition");
    return ESP_OK;
  }
  ESP_LOGW(kTag, "OTA: writing %u bytes to %s", static_cast<unsigned>(total),
           target->label);

  esp_ota_handle_t handle = 0;
  if (esp_ota_begin(target, total, &handle) != ESP_OK) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                        "esp_ota_begin failed");
    return ESP_OK;
  }

  constexpr size_t kChunk = 2048;
  char* chunk = static_cast<char*>(std::malloc(kChunk));
  if (chunk == nullptr) {
    esp_ota_abort(handle);
    return httpd_resp_send_500(req);
  }
  size_t remaining = total;
  bool failed = false;
  int timeouts = 0;
  while (remaining > 0) {
    const size_t want = remaining < kChunk ? remaining : kChunk;
    const int got = httpd_req_recv(req, chunk, want);
    if (got == HTTPD_SOCK_ERR_TIMEOUT) {
      // A stall part-way through a several-megabyte upload is normal on a
      // busy 2.4 GHz link; only give up once it stops recovering.
      if (++timeouts > 10) {
        failed = true;
        break;
      }
      continue;
    }
    if (got <= 0) {
      failed = true;
      break;
    }
    timeouts = 0;
    if (esp_ota_write(handle, chunk, static_cast<size_t>(got)) != ESP_OK) {
      failed = true;
      break;
    }
    remaining -= static_cast<size_t>(got);
  }
  std::free(chunk);

  if (failed) {
    esp_ota_abort(handle);
    ESP_LOGE(kTag, "OTA: transfer failed with %u bytes left",
             static_cast<unsigned>(remaining));
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "write failed");
    return ESP_OK;
  }
  if (esp_ota_end(handle) != ESP_OK) {
    ESP_LOGE(kTag, "OTA: image validation failed");
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid image");
    return ESP_OK;
  }
  if (esp_ota_set_boot_partition(target) != ESP_OK) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                        "set_boot_partition failed");
    return ESP_OK;
  }
  neon_config_flush_now();
  ESP_LOGW(kTag, "OTA: installed, rebooting into %s", target->label);
  send_json(req, "{\"ok\":true,\"rebooting\":true}");
  xTaskCreate(restart_task, "ota_reboot", 2048, nullptr, 5, nullptr);
  return ESP_OK;
}

// JitterBuffer::State, as a word the editor and the panel both show.
const char* sub_state_str(uint8_t state) {
  switch (state) {
    case 1:
      return "buffering";
    case 2:
      return "playing";
    default:
      return "idle";
  }
}

// GET /api/audio/channels — Link Audio discovery for the subscribe picker.
esp_err_t handle_audio_channels(httpd_req_t* req) {
  constexpr size_t kCap = 2048;
  char* buf = static_cast<char*>(std::malloc(kCap));
  if (buf == nullptr) {
    return httpd_resp_send_500(req);
  }
  const int n = neon_audio_channels_json(buf, static_cast<int>(kCap));
  httpd_resp_set_type(req, "application/json");
  const esp_err_t err =
      n > 0 ? httpd_resp_send(req, buf, n)
            : httpd_resp_send(req, "{\"available\":false,\"channels\":[]}",
                              HTTPD_RESP_USE_STRLEN);
  std::free(buf);
  return err;
}

esp_err_t handle_status(httpd_req_t* req) {
  neon::TimelineSnapshot tl;
  timeline_bus().read(tl);
  const uint64_t mpb_us = (tl.tempo_mpb_q32 + (1ull << 31)) >> 32;
  const uint32_t mbpm =
      mpb_us != 0 ? static_cast<uint32_t>(60000000000ull / mpb_us) : 0;
  const neon::ActiveNet net = netman::preference().active();

  char ip[16] = {};
  netman::primary_ip(ip, sizeof(ip));
  const bool setup_ap = netman::ap_is_up();
  const auto& cfg = neon_config();
  char ssid[68] = {};
  char ap_ssid[68] = {};
  char fw[40] = {};
  std::strncpy(fw, firmware_version(), sizeof(fw) - 1);
  json_escape(neon_wifi_current_ssid(), ssid, sizeof(ssid));
  json_escape(netman::ap_ssid(), ap_ssid, sizeof(ap_ssid));
  const unsigned disc = neon_wifi_last_disconnect_reason();

  // Phase and quantum let the web strip draw the same bar the panel does,
  // from the same numbers (neon::phase_milli_beats).
  const uint32_t phase = neon::phase_milli_beats(tl, esp_timer_get_time());
  const uint32_t quantum = tl.quantum_beats != 0 ? tl.quantum_beats : 4;

  const halesp::PulseStats ps = halesp::pulse_stats();
  neon::AudioStatus audio;
  audio_status_bus().read(audio);
  char buf[1408];
  const int n = std::snprintf(
      buf, sizeof(buf),
      "{\"bpm\":%u.%03u,\"peers\":%u,\"playing\":%s,\"network\":\"%s\","
      "\"ext_clock\":%s,\"uptime_s\":%lld,"
      "\"phase_milli\":%u,\"quantum\":%u,\"tempo_valid\":%s,"
      "\"hostname\":\"%s.local\",\"device_name\":\"%s\",\"ip\":\"%s\","
      "\"setup_ap\":%s,\"ap_ssid\":\"%s\","
      "\"wifi_ssid\":\"%s\",\"wifi_pass_len\":%u,\"wifi_fail_reason\":%u,"
      "\"firmware\":\"%s\",\"set_bpm\":%u.%03u,"
      "\"pulse\":{\"edges\":%u,\"late_max_us\":%u,\"late_avg_us\":%u},"
      "\"audio\":{\"running\":%s,\"underruns\":%u,\"peak_l\":%u,"
      "\"peak_r\":%u,\"publishing\":%s,\"subscribers\":%u,"
      "\"sub_state\":\"%s\",\"sub_rate\":%u,\"sub_dropped\":%u,"
      "\"fill_ms\":%u,\"clock_ppm\":%d,\"rx_dropped\":%u,"
      "\"jit_underruns\":%u,\"tx_dropped\":%u,\"trim_ppm\":%d}}",
      static_cast<unsigned>(mbpm / 1000), static_cast<unsigned>(mbpm % 1000),
      static_cast<unsigned>(app_status_peers()),
      tl.playing != 0 ? "true" : "false",
      net == neon::ActiveNet::kEthernet ? "ethernet"
      : net == neon::ActiveNet::kWifi   ? "wifi"
                                        : "none",
      app_status_ext_clock() ? "true" : "false",
      static_cast<long long>(esp_timer_get_time() / 1000000),
      static_cast<unsigned>(phase), static_cast<unsigned>(quantum),
      tl.tempo_mpb_q32 != 0 ? "true" : "false", cfg.device_name,
      cfg.device_name, ip, setup_ap ? "true" : "false", ap_ssid, ssid,
      static_cast<unsigned>(std::strlen(cfg.wifi[0].pass)), disc, fw,
      static_cast<unsigned>(cfg.tempo_milli_bpm / 1000),
      static_cast<unsigned>(cfg.tempo_milli_bpm % 1000),
      static_cast<unsigned>(ps.edges), static_cast<unsigned>(ps.late_max_us),
      static_cast<unsigned>(ps.late_avg_us),
      audio.running != 0 ? "true" : "false",
      static_cast<unsigned>(audio.underruns),
      static_cast<unsigned>(audio.peak_l), static_cast<unsigned>(audio.peak_r),
      audio.publishing != 0 ? "true" : "false",
      static_cast<unsigned>(audio.subscribers), sub_state_str(audio.sub_state),
      static_cast<unsigned>(audio.sub_rate),
      static_cast<unsigned>(audio.sub_dropped),
      static_cast<unsigned>(audio.fill_ms),
      static_cast<int>(audio.clock_ppm),
      static_cast<unsigned>(audio.rx_dropped),
      static_cast<unsigned>(audio.jit_underruns),
      static_cast<unsigned>(audio.tx_dropped),
      static_cast<int>(audio.trim_ppm));
  if (n < 0) {
    return httpd_resp_send_500(req);
  }
  // snprintf returns the length it *would* have written, so an oversized
  // payload (a long SSID, say) would otherwise send past the end of buf.
  const size_t len = static_cast<size_t>(n) < sizeof(buf)
                         ? static_cast<size_t>(n)
                         : sizeof(buf) - 1;
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, buf, len);
}

// POST /api/reboot — soft reset so WiFi STA creds take effect without
// yanking the USB cable.
esp_err_t handle_reboot(httpd_req_t* req) {
  // Never reboot with a pending debounced write still in RAM only.
  neon_config_flush_now();
  send_json(req, "{\"ok\":true,\"rebooting\":true}");
  xTaskCreate(restart_task, "reboot", 2048, nullptr, 5, nullptr);
  return ESP_OK;
}

// POST /api/preset?op=save|recall&slot=0..3
esp_err_t handle_preset(httpd_req_t* req) {
  char op[16] = {};
  char slot_s[8] = {};
  if (!query_param(req, "op", op, sizeof(op)) ||
      !query_param(req, "slot", slot_s, sizeof(slot_s))) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "op and slot required");
    return ESP_OK;
  }
  const int slot = std::atoi(slot_s);
  bool ok = false;
  if (std::strcmp(op, "save") == 0) {
    ok = neon_preset_save(slot);
  } else if (std::strcmp(op, "recall") == 0) {
    ok = neon_preset_recall(slot);
  }
  if (!ok) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad op/slot or empty");
    return ESP_OK;
  }
  return send_ok(req);
}

}  // namespace

void webui_start() {
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.stack_size = 8192;
  cfg.lru_purge_enable = true;
  cfg.max_uri_handlers = 16;
  // An OTA image takes a while to push over WiFi.
  cfg.recv_wait_timeout = 20;
  cfg.send_wait_timeout = 20;

  httpd_handle_t server = nullptr;
  if (httpd_start(&server, &cfg) != ESP_OK) {
    ESP_LOGE(kTag, "httpd start failed");
    return;
  }

  const httpd_uri_t index_uri = {
      .uri = "/", .method = HTTP_GET, .handler = handle_index, .user_ctx = nullptr};
  const httpd_uri_t get_cfg = {.uri = "/api/config",
                               .method = HTTP_GET,
                               .handler = handle_get_config,
                               .user_ctx = nullptr};
  const httpd_uri_t put_cfg = {.uri = "/api/config",
                               .method = HTTP_PUT,
                               .handler = handle_put_config,
                               .user_ctx = nullptr};
  const httpd_uri_t status_uri = {.uri = "/api/status",
                                  .method = HTTP_GET,
                                  .handler = handle_status,
                                  .user_ctx = nullptr};
  const httpd_uri_t preset_uri = {.uri = "/api/preset",
                                  .method = HTTP_POST,
                                  .handler = handle_preset,
                                  .user_ctx = nullptr};
  const httpd_uri_t reboot_uri = {.uri = "/api/reboot",
                                  .method = HTTP_POST,
                                  .handler = handle_reboot,
                                  .user_ctx = nullptr};
  const httpd_uri_t transport_uri = {.uri = "/api/transport",
                                     .method = HTTP_POST,
                                     .handler = handle_transport,
                                     .user_ctx = nullptr};
  const httpd_uri_t tempo_uri = {.uri = "/api/tempo",
                                 .method = HTTP_POST,
                                 .handler = handle_tempo,
                                 .user_ctx = nullptr};
  const httpd_uri_t resync_uri = {.uri = "/api/resync",
                                  .method = HTTP_POST,
                                  .handler = handle_resync,
                                  .user_ctx = nullptr};
  const httpd_uri_t scan_uri = {.uri = "/api/scan",
                                .method = HTTP_GET,
                                .handler = handle_scan,
                                .user_ctx = nullptr};
  const httpd_uri_t reset_uri = {.uri = "/api/factory_reset",
                                 .method = HTTP_POST,
                                 .handler = handle_factory_reset,
                                 .user_ctx = nullptr};
  const httpd_uri_t channels_uri = {.uri = "/api/audio/channels",
                                    .method = HTTP_GET,
                                    .handler = handle_audio_channels,
                                    .user_ctx = nullptr};
  const httpd_uri_t ota_uri = {.uri = "/api/ota",
                               .method = HTTP_POST,
                               .handler = handle_ota,
                               .user_ctx = nullptr};
  httpd_register_uri_handler(server, &index_uri);
  httpd_register_uri_handler(server, &get_cfg);
  httpd_register_uri_handler(server, &put_cfg);
  httpd_register_uri_handler(server, &status_uri);
  httpd_register_uri_handler(server, &preset_uri);
  httpd_register_uri_handler(server, &reboot_uri);
  httpd_register_uri_handler(server, &transport_uri);
  httpd_register_uri_handler(server, &tempo_uri);
  httpd_register_uri_handler(server, &resync_uri);
  httpd_register_uri_handler(server, &scan_uri);
  httpd_register_uri_handler(server, &reset_uri);
  httpd_register_uri_handler(server, &channels_uri);
  httpd_register_uri_handler(server, &ota_uri);

  // Mark this image good once the editor is serving: a bad OTA that never
  // gets this far is rolled back to the previous slot on the next boot.
  esp_ota_mark_app_valid_cancel_rollback();

  char ip[16] = {};
  netman::primary_ip(ip, sizeof(ip));
  ESP_LOGI(kTag, "web editor up (http://%s.local/ / http://%s/) fw=%s",
           neon_config().device_name, ip[0] ? ip : "…", firmware_version());
}
