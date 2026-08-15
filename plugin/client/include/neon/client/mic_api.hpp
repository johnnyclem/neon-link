#pragma once

#include <string>
#include <vector>

#include "neon/client/api.hpp"
#include "neon/client/http.hpp"
#include "neon/client/mic.hpp"

namespace neon::client {

class MicClient {
 public:
  explicit MicClient(HttpTransport& http);

  void setTarget(const char* host, int port = kMicDefaultPort);

  const std::string& host() const { return host_; }
  int port() const { return port_; }

  Result<MicStatus> getStatus();
  Result<MicConfig> getConfig();
  Result<MicConfig> putConfig(const MicConfig& from, const MicConfig& to);
  Result<MicConfig> putConfigJson(const char* json);
  Result<std::vector<MicSourceRow>> getSources();
  Result<CaptureReply> capture(CaptureOp op);

 private:
  HttpTransport& http_;
  std::string host_ = kMicDefaultHost;
  int port_ = kMicDefaultPort;

  template <typename T>
  Result<T> fill_error(const HttpResponse& r, const char* what) const;
};

}  // namespace neon::client
