#include "neon/client/http.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace neon::client {
namespace {

std::string to_lower(std::string s) {
  for (char& c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

}  // namespace

std::string format_request(const char* method, const char* host,
                           const char* path, const char* body) {
  const size_t n = body != nullptr ? std::strlen(body) : 0;
  std::ostringstream os;
  os << method << ' ' << (path != nullptr ? path : "/") << " HTTP/1.1\r\n"
     << "Host: " << (host != nullptr ? host : "") << "\r\n"
     << "Content-Length: " << n << "\r\n"
     << "Connection: keep-alive\r\n";
  if (n > 0) {
    os << "Content-Type: application/json\r\n";
  }
  os << "\r\n";
  if (n > 0) {
    os.write(body, static_cast<std::streamsize>(n));
  }
  return os.str();
}

bool parse_response(const char* raw, size_t len, HttpResponse* out) {
  if (raw == nullptr || out == nullptr) {
    return false;
  }
  *out = HttpResponse{};
  const char* hdr_end = nullptr;
  for (size_t i = 0; i + 3 < len; ++i) {
    if (raw[i] == '\r' && raw[i + 1] == '\n' && raw[i + 2] == '\r' &&
        raw[i + 3] == '\n') {
      hdr_end = raw + i;
      break;
    }
  }
  if (hdr_end == nullptr) {
    out->error = "truncated headers";
    return false;
  }
  const std::string headers(raw, hdr_end);
  std::istringstream hs(headers);
  std::string line;
  if (!std::getline(hs, line)) {
    out->error = "empty status line";
    return false;
  }
  if (!line.empty() && line.back() == '\r') {
    line.pop_back();
  }
  // HTTP/1.1 200 OK
  const auto sp1 = line.find(' ');
  const auto sp2 = line.find(' ', sp1 == std::string::npos ? 0 : sp1 + 1);
  if (sp1 == std::string::npos) {
    out->error = "bad status line";
    return false;
  }
  try {
    out->status = std::stoi(line.substr(
        sp1 + 1, (sp2 == std::string::npos ? line.size() : sp2) - sp1 - 1));
  } catch (...) {
    out->error = "bad status code";
    return false;
  }

  int content_len = -1;
  while (std::getline(hs, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    const auto colon = line.find(':');
    if (colon == std::string::npos) {
      continue;
    }
    std::string key = to_lower(line.substr(0, colon));
    std::string val = line.substr(colon + 1);
    while (!val.empty() && (val.front() == ' ' || val.front() == '\t')) {
      val.erase(val.begin());
    }
    if (key == "content-length") {
      try {
        content_len = std::stoi(val);
      } catch (...) {
        out->error = "bad Content-Length";
        return false;
      }
    } else if (key == "transfer-encoding" && to_lower(val) == "chunked") {
      out->error = "chunked encoding not supported";
      return false;
    }
  }
  if (content_len < 0) {
    out->error = "missing Content-Length";
    return false;
  }
  const char* body = hdr_end + 4;
  const size_t have = static_cast<size_t>(raw + len - body);
  if (have < static_cast<size_t>(content_len)) {
    out->error = "truncated body";
    return false;
  }
  out->body.assign(body, static_cast<size_t>(content_len));
  return true;
}

void prefer_inet(int* families, int n) {
  if (families == nullptr || n <= 0) {
    return;
  }
  std::stable_partition(families, families + n, [](int f) {
#ifdef AF_INET
    return f == AF_INET;
#else
    return f == 2;  // POSIX AF_INET
#endif
  });
}

#ifndef _WIN32

namespace {

void set_nonblock(int fd) {
  const int flags = fcntl(fd, F_GETFL, 0);
  if (flags >= 0) {
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  }
}

int64_t now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

// Remaining budget until `deadline_ms`. 0 means the caller must fail now.
int remain_ms(int64_t deadline_ms) {
  const int64_t left = deadline_ms - now_ms();
  if (left <= 0) {
    return 0;
  }
  return left > 60000 ? 60000 : static_cast<int>(left);
}

bool wait_fd(int fd, short events, int timeout_ms) {
  if (timeout_ms <= 0) {
    return false;
  }
  pollfd p{};
  p.fd = fd;
  p.events = events;
  const int r = poll(&p, 1, timeout_ms);
  return r > 0 && (p.revents & events) != 0;
}

bool looks_ipv4(const char* host);

// Bound mDNS. A stuck getaddrinfo on *.local is what made Bind look
// dead and Save hang the VST — the name server never answers while
// Link Audio owns the radio. The resolver thread is detached on
// timeout; Job frees the result if it lands later.
bool resolve_addrs(const char* host, const char* port, addrinfo** out,
                   int timeout_ms) {
  if (out == nullptr || host == nullptr) {
    return false;
  }
  *out = nullptr;
  if (looks_ipv4(host)) {
    addrinfo hints{};
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_INET;
    hints.ai_flags = AI_NUMERICHOST;
    return getaddrinfo(host, port, &hints, out) == 0 && *out != nullptr;
  }
  struct Job {
    std::string host;
    std::string port;
    addrinfo* res = nullptr;
    int err = EAI_FAIL;
    std::atomic<bool> done{false};
    ~Job() {
      if (res != nullptr) {
        freeaddrinfo(res);
      }
    }
  };
  auto job = std::make_shared<Job>();
  job->host = host;
  job->port = port != nullptr ? port : "80";
  std::thread([job] {
    addrinfo hints{};
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_INET;
    job->err = getaddrinfo(job->host.c_str(), job->port.c_str(), &hints,
                           &job->res);
    job->done.store(true, std::memory_order_release);
  }).detach();
  const int64_t deadline = now_ms() + (timeout_ms > 0 ? timeout_ms : 400);
  while (!job->done.load(std::memory_order_acquire) && now_ms() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  if (!job->done.load(std::memory_order_acquire)) {
    return false;
  }
  if (job->err != 0 || job->res == nullptr) {
    return false;
  }
  *out = job->res;
  job->res = nullptr;
  return true;
}

bool looks_ipv4(const char* host) {
  if (host == nullptr || host[0] == '\0') {
    return false;
  }
  int dots = 0;
  int digits = 0;
  for (const char* p = host; *p != '\0'; ++p) {
    if (*p == '.') {
      if (digits == 0) {
        return false;
      }
      ++dots;
      digits = 0;
    } else if (*p >= '0' && *p <= '9') {
      ++digits;
    } else {
      return false;
    }
  }
  return dots == 3 && digits > 0;
}

}  // namespace

PosixHttpTransport::~PosixHttpTransport() { close(); }

void PosixHttpTransport::close() {
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
  host_.clear();
  port_ = 0;
}

HttpResponse PosixHttpTransport::request(const char* method, const char* host,
                                         int port, const char* path,
                                         const char* body, int timeout_ms) {
  HttpResponse out;
  if (host == nullptr || host[0] == '\0') {
    out.connect_failed = true;
    out.error = "empty host";
    return out;
  }
  if (port <= 0) {
    port = 80;
  }
  if (timeout_ms <= 0) {
    timeout_ms = 800;
  }
  const int64_t deadline = now_ms() + timeout_ms;

  const bool reuse =
      fd_ >= 0 && host_ == host && port_ == port;

  if (!reuse) {
    close();
    if (remain_ms(deadline) <= 0) {
      out.timed_out = true;
      out.error = "connect timeout";
      return out;
    }
    addrinfo* res = nullptr;
    const std::string port_s = std::to_string(port);
    // Cap name lookup so Bind cannot stall the HTTP thread. Numeric
    // hosts skip this entirely (AI_NUMERICHOST inside resolve_addrs).
    const int name_budget = remain_ms(deadline);
    const int name_ms = looks_ipv4(host) ? name_budget
                                         : (name_budget < 400 ? name_budget : 400);
    if (!resolve_addrs(host, port_s.c_str(), &res, name_ms) || res == nullptr) {
      out.connect_failed = true;
      out.timed_out = !looks_ipv4(host);
      out.error = looks_ipv4(host) ? "connect failed"
                                   : "name lookup timed out — use the module IP";
      return out;
    }
    std::vector<addrinfo*> order;
    for (addrinfo* p = res; p != nullptr; p = p->ai_next) {
      if (p->ai_family == AF_INET) {
        order.push_back(p);
      }
    }
    for (addrinfo* p = res; p != nullptr; p = p->ai_next) {
      if (p->ai_family != AF_INET) {
        order.push_back(p);
      }
    }

    int fd = -1;
    for (addrinfo* p : order) {
      fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
      if (fd < 0) {
        continue;
      }
      set_nonblock(fd);
      const int cr = ::connect(fd, p->ai_addr, p->ai_addrlen);
      if (cr == 0 || errno == EINPROGRESS) {
        if (wait_fd(fd, POLLOUT, remain_ms(deadline))) {
          int err = 0;
          socklen_t elen = sizeof(err);
          if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &elen) == 0 &&
              err == 0) {
            break;
          }
        }
      }
      ::close(fd);
      fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) {
      out.connect_failed = true;
      out.error = "connect failed";
      return out;
    }
    fd_ = fd;
    host_ = host;
    port_ = port;
  }

  const std::string req = format_request(method, host, path, body);
  size_t sent = 0;
  while (sent < req.size()) {
    if (!wait_fd(fd_, POLLOUT, remain_ms(deadline))) {
      out.timed_out = true;
      out.error = "send timeout";
      close();
      return out;
    }
    const ssize_t n =
        ::send(fd_, req.data() + sent, req.size() - sent, 0);
    if (n < 0) {
      if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
        continue;
      }
      out.connect_failed = true;
      out.error = std::strerror(errno);
      close();
      return out;
    }
    sent += static_cast<size_t>(n);
  }

  std::string raw;
  raw.reserve(2048);
  char tmp[1024];
  int content_len = -1;
  size_t hdr = std::string::npos;
  while (true) {
    if (!wait_fd(fd_, POLLIN, remain_ms(deadline))) {
      out.timed_out = true;
      out.error = "recv timeout";
      close();
      return out;
    }
    const ssize_t n = ::recv(fd_, tmp, sizeof(tmp), 0);
    if (n == 0) {
      break;
    }
    if (n < 0) {
      if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
        continue;
      }
      out.connect_failed = true;
      out.error = std::strerror(errno);
      close();
      return out;
    }
    raw.append(tmp, static_cast<size_t>(n));
    if (hdr == std::string::npos) {
      hdr = raw.find("\r\n\r\n");
    }
    if (hdr != std::string::npos && content_len < 0) {
      const std::string headers = raw.substr(0, hdr);
      std::istringstream hs(headers);
      std::string line;
      std::getline(hs, line);
      while (std::getline(hs, line)) {
        if (!line.empty() && line.back() == '\r') {
          line.pop_back();
        }
        auto colon = line.find(':');
        if (colon == std::string::npos) {
          continue;
        }
        std::string key = to_lower(line.substr(0, colon));
        if (key == "content-length") {
          std::string val = line.substr(colon + 1);
          while (!val.empty() && (val.front() == ' ' || val.front() == '\t')) {
            val.erase(val.begin());
          }
          try {
            content_len = std::stoi(val);
          } catch (...) {
            content_len = -1;
          }
        }
      }
    }
    if (hdr != std::string::npos && content_len >= 0) {
      const size_t need = hdr + 4 + static_cast<size_t>(content_len);
      if (raw.size() >= need) {
        break;
      }
    }
  }

  if (!parse_response(raw.data(), raw.size(), &out)) {
    close();
    return out;
  }
  return out;
}

#else

PosixHttpTransport::~PosixHttpTransport() = default;
void PosixHttpTransport::close() {}
HttpResponse PosixHttpTransport::request(const char*, const char*, int,
                                         const char*, const char*, int) {
  HttpResponse r;
  r.connect_failed = true;
  r.error = "POSIX sockets not available";
  return r;
}

#endif

}  // namespace neon::client
