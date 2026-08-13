#include "neon/client/api.hpp"

#include <cstdio>
#include <cstring>

#include "neon/config/json.hpp"

namespace neon::client {
namespace {

constexpr int kStatusTimeoutMs = 800;
constexpr int kConfigTimeoutMs = 1500;
constexpr int kCmdTimeoutMs = 800;
constexpr int kScanTimeoutMs = 8000;

const char* transport_path(TransportOp op) {
  switch (op) {
    case TransportOp::Play:
      return "/api/transport?op=play";
    case TransportOp::Stop:
      return "/api/transport?op=stop";
    case TransportOp::Toggle:
      return "/api/transport?op=toggle";
    case TransportOp::PlayNow:
      return "/api/transport?op=play_now";
    case TransportOp::StopNow:
      return "/api/transport?op=stop_now";
  }
  return "/api/transport?op=toggle";
}

const char* tempo_path(TempoOp op, int delta, char* buf, size_t cap) {
  switch (op) {
    case TempoOp::Tap:
      return "/api/tempo?op=tap";
    case TempoOp::Double:
      return "/api/tempo?op=double";
    case TempoOp::Half:
      return "/api/tempo?op=half";
    case TempoOp::Nudge:
      std::snprintf(buf, cap, "/api/tempo?op=nudge&delta=%d", delta);
      return buf;
  }
  return "/api/tempo?op=tap";
}

}  // namespace

DeviceClient::DeviceClient(HttpTransport& http) : http_(http) {}

void DeviceClient::setTarget(const char* host, int port) {
  if (host != nullptr && host[0] != '\0') {
    host_ = host;
  }
  port_ = port > 0 ? port : 80;
}

template <typename T>
Result<T> DeviceClient::fill_error(const HttpResponse& r,
                                   const char* what) const {
  Result<T> out;
  out.http_status = r.status;
  out.timed_out = r.timed_out;
  out.connect_failed = r.connect_failed;
  if (!r.error.empty()) {
    out.error = r.error;
  } else if (r.status != 0) {
    out.error = std::string(what) + " → HTTP " + std::to_string(r.status);
  } else {
    out.error = what;
  }
  return out;
}

Result<Status> DeviceClient::getStatus() {
  const HttpResponse r = http_.request("GET", host_.c_str(), port_,
                                       "/api/status", nullptr, kStatusTimeoutMs);
  if (r.timed_out || r.connect_failed || r.status != 200) {
    return fill_error<Status>(r, "GET /api/status");
  }
  Result<Status> out;
  out.http_status = r.status;
  if (!parse_status(r.body.c_str(), r.body.size(), &out.value)) {
    out.error = "status parse failed";
    return out;
  }
  persist_lazy_ = out.value.persist_lazy;
  out.ok = true;
  return out;
}

Result<neon::Config> DeviceClient::getConfig() {
  const HttpResponse r = http_.request("GET", host_.c_str(), port_,
                                       "/api/config", nullptr, kConfigTimeoutMs);
  if (r.timed_out || r.connect_failed || r.status != 200) {
    return fill_error<neon::Config>(r, "GET /api/config");
  }
  Result<neon::Config> out;
  out.http_status = r.status;
  neon::Config cfg{};
  if (!neon::config_from_json(r.body.c_str(), r.body.size(), &cfg)) {
    out.error = "config parse failed";
    return out;
  }
  out.value = cfg;
  out.ok = true;
  return out;
}

Result<neon::Config> DeviceClient::putConfig(const JsonPatch& patch,
                                            Persist persist) {
  if (persist == Persist::Lazy && !persist_lazy_) {
    Result<neon::Config> refused;
    refused.error = "persist_lazy not advertised";
    return refused;
  }
  const std::string body = patch.toJson();
  const char* path = persist == Persist::Lazy ? "/api/config?persist=lazy"
                                              : "/api/config";
  const HttpResponse r = http_.request("PUT", host_.c_str(), port_, path,
                                       body.c_str(), kConfigTimeoutMs);
  if (r.timed_out || r.connect_failed || r.status != 200) {
    return fill_error<neon::Config>(r, "PUT /api/config");
  }
  Result<neon::Config> out;
  out.http_status = r.status;
  neon::Config cfg{};
  if (!neon::config_from_json(r.body.c_str(), r.body.size(), &cfg)) {
    out.error = "config echo parse failed";
    return out;
  }
  out.value = cfg;
  out.ok = true;
  return out;
}

Result<void> DeviceClient::cmd(const char* path, int timeout_ms) {
  const HttpResponse r =
      http_.request("POST", host_.c_str(), port_, path, nullptr, timeout_ms);
  if (r.timed_out || r.connect_failed || (r.status != 200 && r.status != 0)) {
    // status 0 + empty can happen on reboot (connection drop).
    if (r.status != 0 || (!r.timed_out && !r.connect_failed)) {
      auto e = fill_error<int>(r, path);
      Result<void> out;
      out.http_status = e.http_status;
      out.timed_out = e.timed_out;
      out.connect_failed = e.connect_failed;
      out.error = std::move(e.error);
      return out;
    }
  }
  if (r.status == 200 || r.connect_failed) {
    Result<void> out;
    out.ok = true;
    out.http_status = r.status;
    out.connect_failed = r.connect_failed;
    return out;
  }
  auto e = fill_error<int>(r, path);
  Result<void> out;
  out.http_status = e.http_status;
  out.timed_out = e.timed_out;
  out.connect_failed = e.connect_failed;
  out.error = std::move(e.error);
  return out;
}

Result<void> DeviceClient::transport(TransportOp op) {
  return cmd(transport_path(op), kCmdTimeoutMs);
}

Result<void> DeviceClient::setTempo(double bpm) {
  char path[64];
  std::snprintf(path, sizeof(path), "/api/tempo?bpm=%.3f", bpm);
  return cmd(path, kCmdTimeoutMs);
}

Result<void> DeviceClient::tempoOp(TempoOp op, int delta) {
  char buf[64];
  return cmd(tempo_path(op, delta, buf, sizeof(buf)), kCmdTimeoutMs);
}

Result<void> DeviceClient::resync(ResyncOp op) {
  return cmd(op == ResyncOp::Now ? "/api/resync?op=now" : "/api/resync?op=next",
             kCmdTimeoutMs);
}

Result<void> DeviceClient::preset(PresetOp op, int slot) {
  if (slot < 0 || slot > 3) {
    Result<void> out;
    out.error = "preset slot out of range";
    return out;
  }
  char path[48];
  std::snprintf(path, sizeof(path), "/api/preset?op=%s&slot=%d",
                op == PresetOp::Save ? "save" : "recall", slot);
  return cmd(path, kCmdTimeoutMs);
}

Result<std::vector<ScanResult>> DeviceClient::scan() {
  const HttpResponse r = http_.request("GET", host_.c_str(), port_, "/api/scan",
                                       nullptr, kScanTimeoutMs);
  if (r.timed_out || r.connect_failed || r.status != 200) {
    return fill_error<std::vector<ScanResult>>(r, "GET /api/scan");
  }
  Result<std::vector<ScanResult>> out;
  out.http_status = r.status;
  if (!parse_scan(r.body.c_str(), r.body.size(), &out.value)) {
    out.error = "scan parse failed";
    return out;
  }
  out.ok = true;
  return out;
}

Result<AudioChannels> DeviceClient::audioChannels() {
  const HttpResponse r =
      http_.request("GET", host_.c_str(), port_, "/api/audio/channels", nullptr,
                    kStatusTimeoutMs);
  if (r.timed_out || r.connect_failed || r.status != 200) {
    return fill_error<AudioChannels>(r, "GET /api/audio/channels");
  }
  Result<AudioChannels> out;
  out.http_status = r.status;
  if (!parse_audio_channels(r.body.c_str(), r.body.size(), &out.value)) {
    out.error = "audio channels parse failed";
    return out;
  }
  out.ok = true;
  return out;
}

Result<void> DeviceClient::reboot() {
  const HttpResponse r = http_.request("POST", host_.c_str(), port_,
                                       "/api/reboot", nullptr, kCmdTimeoutMs);
  // The module drops the connection mid-reboot. That is success.
  Result<void> out;
  out.ok = true;
  out.http_status = r.status;
  out.connect_failed = r.connect_failed;
  out.timed_out = r.timed_out;
  return out;
}

}  // namespace neon::client
