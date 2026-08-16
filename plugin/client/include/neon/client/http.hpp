#pragma once

#include <cstddef>
#include <string>

namespace neon::client {

struct HttpResponse {
  int status = 0;
  std::string body;
  std::string error;
  bool timed_out = false;
  bool connect_failed = false;
};

// Seamed so tests inject a fake. POSIX impl is PosixHttpTransport.
class HttpTransport {
 public:
  virtual ~HttpTransport() = default;
  // host is an IPv4 literal or a DNS name. POSIX impl uses getaddrinfo
  // and prefers AF_INET (macOS often returns AAAA first for *.local;
  // the module httpd is IPv4).
  //
  // extra_header, when non-null, is one complete "Name: value" line (no
  // trailing CRLF) folded into the request. Currently only DeviceClient's
  // factoryReset() uses it, to carry X-Neon-Token — the device-side gate
  // added alongside Config::device_token so a non-browser client cannot
  // hit /api/ota or /api/factory_reset just by setting Host to whatever
  // the module expects (components/web_ui/src/web_ui.cpp's
  // check_device_token). The plugin is exactly such a non-browser client,
  // so it has to send the token like the web editor does.
  virtual HttpResponse request(const char* method, const char* host, int port,
                               const char* path, const char* body,
                               int timeout_ms,
                               const char* extra_header = nullptr) = 0;
};

// HTTP/1.1 subset used by PosixHttpTransport. Exposed for host tests.
// extra_header: see HttpTransport::request.
std::string format_request(const char* method, const char* host,
                           const char* path, const char* body,
                           const char* extra_header = nullptr);

// Parse a complete response (status line + headers + Content-Length body).
// No chunked encoding, no redirects, no TLS, no compression.
bool parse_response(const char* raw, size_t len, HttpResponse* out);

// Stable-sort getaddrinfo results so AF_INET comes first. `head` is the
// original list; returns a newly ordered chain that still owns the same
// nodes (caller frees with freeaddrinfo on the original head only).
// Implemented in http.cpp; tests cover the IPv4-preference helper
// prefer_inet() on a synthetic list of family tags.
void prefer_inet(int* families, int n);

class PosixHttpTransport : public HttpTransport {
 public:
  PosixHttpTransport() = default;
  ~PosixHttpTransport() override;
  PosixHttpTransport(const PosixHttpTransport&) = delete;
  PosixHttpTransport& operator=(const PosixHttpTransport&) = delete;

  HttpResponse request(const char* method, const char* host, int port,
                       const char* path, const char* body, int timeout_ms,
                       const char* extra_header = nullptr) override;

  void close();

 private:
  int fd_ = -1;
  std::string host_;
  int port_ = 0;
};

}  // namespace neon::client
