#include "webui/web_ui.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "app_state/config_store.h"
#include "app_state/timeline_bus.h"
#include "halesp/pulse_hw_gptimer.hpp"
#include "neon/config/json.hpp"
#include "neon/net/preference.hpp"
#include "netman/net_manager.h"

namespace {

const char* kTag = "web_ui";

extern "C" {
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");
}

esp_err_t handle_index(httpd_req_t* req) {
  httpd_resp_set_type(req, "text/html");
  const size_t len =
      static_cast<size_t>(index_html_end - index_html_start) - 1;  // NUL
  return httpd_resp_send(req, reinterpret_cast<const char*>(index_html_start),
                         len);
}

esp_err_t handle_get_config(httpd_req_t* req) {
  char* buf = static_cast<char*>(std::malloc(4096));
  if (buf == nullptr) {
    return httpd_resp_send_500(req);
  }
  const size_t n = neon::config_to_json(neon_config(), buf, 4096);
  httpd_resp_set_type(req, "application/json");
  const esp_err_t err =
      n != 0 ? httpd_resp_send(req, buf, n) : httpd_resp_send_500(req);
  std::free(buf);
  return err;
}

esp_err_t handle_put_config(httpd_req_t* req) {
  if (req->content_len == 0 || req->content_len > 8192) {
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
  neon_config_apply(cfg);
  ESP_LOGI(kTag, "config updated from web editor");
  return handle_get_config(req);  // respond with the sanitized result
}

esp_err_t handle_status(httpd_req_t* req) {
  neon::TimelineSnapshot tl;
  timeline_bus().read(tl);
  const uint64_t mpb_us = (tl.tempo_mpb_q32 + (1ull << 31)) >> 32;
  const uint32_t mbpm =
      mpb_us != 0 ? static_cast<uint32_t>(60000000000ull / mpb_us) : 0;
  const neon::ActiveNet net = netman::preference().active();

  const halesp::PulseStats ps = halesp::pulse_stats();
  char buf[384];
  const int n = std::snprintf(
      buf, sizeof(buf),
      "{\"bpm\":%u.%03u,\"peers\":%u,\"playing\":%s,\"network\":\"%s\","
      "\"ext_clock\":%s,\"uptime_s\":%lld,"
      "\"pulse\":{\"edges\":%u,\"late_max_us\":%u,\"late_avg_us\":%u}}",
      static_cast<unsigned>(mbpm / 1000), static_cast<unsigned>(mbpm % 1000),
      static_cast<unsigned>(app_status_peers()),
      tl.playing != 0 ? "true" : "false",
      net == neon::ActiveNet::kEthernet ? "ethernet"
      : net == neon::ActiveNet::kWifi   ? "wifi"
                                        : "none",
      app_status_ext_clock() ? "true" : "false",
      static_cast<long long>(esp_timer_get_time() / 1000000),
      static_cast<unsigned>(ps.edges), static_cast<unsigned>(ps.late_max_us),
      static_cast<unsigned>(ps.late_avg_us));
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, buf, n);
}

// POST /api/preset?op=save|recall&slot=0..3
esp_err_t handle_preset(httpd_req_t* req) {
  char query[64] = {};
  char op[16] = {};
  char slot_s[8] = {};
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
      httpd_query_key_value(query, "op", op, sizeof(op)) != ESP_OK ||
      httpd_query_key_value(query, "slot", slot_s, sizeof(slot_s)) !=
          ESP_OK) {
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
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
}

}  // namespace

void webui_start() {
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.stack_size = 8192;
  cfg.lru_purge_enable = true;

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
  httpd_register_uri_handler(server, &index_uri);
  httpd_register_uri_handler(server, &get_cfg);
  httpd_register_uri_handler(server, &put_cfg);
  httpd_register_uri_handler(server, &status_uri);
  httpd_register_uri_handler(server, &preset_uri);
  ESP_LOGI(kTag, "web editor up (http://neon-link.local/)");
}
