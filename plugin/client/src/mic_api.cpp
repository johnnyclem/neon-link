#include "neon/client/mic_api.hpp"

#include <cstring>

namespace neon::client {
namespace {

constexpr int kStatusTimeoutMs = 800;
constexpr int kConfigTimeoutMs = 1500;
constexpr int kCmdTimeoutMs = 800;

const char* capture_op_name(CaptureOp op) {
  switch (op) {
    case CaptureOp::Start:
      return "start";
    case CaptureOp::Stop:
      return "stop";
    case CaptureOp::Toggle:
      return "toggle";
  }
  return "toggle";
}

}  // namespace

MicClient::MicClient(HttpTransport& http) : http_(http) {}

void MicClient::setTarget(const char* host, int port) {
  if (host != nullptr && host[0] != '\0') {
    host_ = host;
  }
  port_ = port > 0 ? port : kMicDefaultPort;
}

template <typename T>
Result<T> MicClient::fill_error(const HttpResponse& r, const char* what) const {
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

Result<MicStatus> MicClient::getStatus() {
  const HttpResponse r = http_.request("GET", host_.c_str(), port_,
                                       "/api/status", nullptr, kStatusTimeoutMs);
  if (r.timed_out || r.connect_failed || r.status != 200) {
    return fill_error<MicStatus>(r, "GET /api/status");
  }
  Result<MicStatus> out;
  out.http_status = r.status;
  const auto kind = probe_document_kind(r.body.c_str(), r.body.size());
  if (kind == DocumentKind::NeonLink) {
    out.error = kKindMismatchModule;
    return out;
  }
  if (kind != DocumentKind::PhoneMic ||
      !parse_mic_status(r.body.c_str(), r.body.size(), &out.value)) {
    out.error = "status is not a phone-mic document";
    return out;
  }
  out.ok = true;
  return out;
}

Result<MicConfig> MicClient::getConfig() {
  const HttpResponse r = http_.request("GET", host_.c_str(), port_,
                                       "/api/config", nullptr, kConfigTimeoutMs);
  if (r.timed_out || r.connect_failed || r.status != 200) {
    return fill_error<MicConfig>(r, "GET /api/config");
  }
  Result<MicConfig> out;
  out.http_status = r.status;
  if (probe_document_kind(r.body.c_str(), r.body.size()) != DocumentKind::PhoneMic ||
      !parse_mic_config(r.body.c_str(), r.body.size(), &out.value)) {
    out.error = "config is not a phone-mic document";
    return out;
  }
  out.ok = true;
  return out;
}

Result<MicConfig> MicClient::putConfig(const MicConfig& from, const MicConfig& to) {
  const std::string body = mic_config_patch_json(from, to);
  return putConfigJson(body.c_str());
}

Result<MicConfig> MicClient::putConfigJson(const char* json) {
  const HttpResponse r = http_.request("PUT", host_.c_str(), port_, "/api/config",
                                       json, kConfigTimeoutMs);
  if (r.timed_out || r.connect_failed || r.status != 200) {
    return fill_error<MicConfig>(r, "PUT /api/config");
  }
  Result<MicConfig> out;
  out.http_status = r.status;
  if (!parse_mic_config(r.body.c_str(), r.body.size(), &out.value)) {
    out.error = "config echo is not a phone-mic document";
    return out;
  }
  out.ok = true;
  return out;
}

Result<std::vector<MicSourceRow>> MicClient::getSources() {
  const HttpResponse r = http_.request("GET", host_.c_str(), port_,
                                       "/api/sources", nullptr, kStatusTimeoutMs);
  if (r.timed_out || r.connect_failed || r.status != 200) {
    return fill_error<std::vector<MicSourceRow>>(r, "GET /api/sources");
  }
  Result<std::vector<MicSourceRow>> out;
  out.http_status = r.status;
  if (!parse_mic_sources(r.body.c_str(), r.body.size(), &out.value)) {
    out.error = "sources parse failed";
    return out;
  }
  out.ok = true;
  return out;
}

Result<CaptureReply> MicClient::capture(CaptureOp op) {
  const std::string body = std::string("{\"op\":\"") + capture_op_name(op) + "\"}";
  const HttpResponse r = http_.request("POST", host_.c_str(), port_, "/api/capture",
                                       body.c_str(), kCmdTimeoutMs);
  if (r.timed_out || r.connect_failed || r.status != 200) {
    return fill_error<CaptureReply>(r, "POST /api/capture");
  }
  Result<CaptureReply> out;
  out.http_status = r.status;
  if (!parse_capture_reply(r.body.c_str(), r.body.size(), &out.value)) {
    out.error = "capture reply parse failed";
    return out;
  }
  out.ok = out.value.ok;
  if (!out.ok && out.error.empty()) {
    out.error = "capture refused";
  }
  return out;
}

}  // namespace neon::client
