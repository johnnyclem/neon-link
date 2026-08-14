#include <doctest.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "neon/client/http.hpp"

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

TEST_CASE("format_request is HTTP/1.1 with Host and Content-Length") {
  const std::string r =
      neon::client::format_request("GET", "stage-left.local", "/api/status",
                                   nullptr);
  CHECK(r.find("GET /api/status HTTP/1.1\r\n") == 0);
  CHECK(r.find("Host: stage-left.local\r\n") != std::string::npos);
  CHECK(r.find("Content-Length: 0\r\n") != std::string::npos);
  CHECK(r.find("Connection: keep-alive\r\n") != std::string::npos);
  CHECK(r.find("\r\n\r\n") != std::string::npos);
  CHECK(r.find("Content-Type:") == std::string::npos);
}

TEST_CASE("format_request PUT includes JSON content type") {
  const std::string r = neon::client::format_request(
      "PUT", "10.0.0.42", "/api/config?persist=lazy",
      "{\"engine\":{\"latency_us\":-500}}");
  CHECK(r.find("PUT /api/config?persist=lazy HTTP/1.1\r\n") == 0);
  CHECK(r.find("Content-Type: application/json\r\n") != std::string::npos);
  CHECK(r.find("{\"engine\":{\"latency_us\":-500}}") != std::string::npos);
}

TEST_CASE("parse_response reads Content-Length body only") {
  const char* raw =
      "HTTP/1.1 200 OK\r\nContent-Length: 11\r\n\r\n{\"ok\":true}TRAILING";
  neon::client::HttpResponse out;
  REQUIRE(neon::client::parse_response(raw, std::strlen(raw), &out));
  CHECK(out.status == 200);
  CHECK(out.body == "{\"ok\":true}");
}

TEST_CASE("parse_response rejects chunked and missing length") {
  neon::client::HttpResponse out;
  const char* chunked =
      "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n";
  CHECK_FALSE(
      neon::client::parse_response(chunked, std::strlen(chunked), &out));
  const char* no_len = "HTTP/1.1 200 OK\r\n\r\nbody";
  CHECK_FALSE(neon::client::parse_response(no_len, std::strlen(no_len), &out));
}

TEST_CASE("prefer_inet sorts AF_INET first and is stable") {
#ifdef AF_INET
  int fams[] = {AF_INET6, AF_INET, AF_INET6, AF_INET};
  neon::client::prefer_inet(fams, 4);
  CHECK(fams[0] == AF_INET);
  CHECK(fams[1] == AF_INET);
  CHECK(fams[2] == AF_INET6);
  CHECK(fams[3] == AF_INET6);
#endif
}

#ifndef _WIN32
TEST_CASE("PosixHttpTransport GET against a local server") {
  int ls = ::socket(AF_INET, SOCK_STREAM, 0);
  REQUIRE(ls >= 0);
  int yes = 1;
  setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  REQUIRE(::bind(ls, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
  REQUIRE(::listen(ls, 1) == 0);
  socklen_t alen = sizeof(addr);
  REQUIRE(getsockname(ls, reinterpret_cast<sockaddr*>(&addr), &alen) == 0);
  const int port = ntohs(addr.sin_port);

  std::thread server([&] {
    int c = ::accept(ls, nullptr, nullptr);
    if (c < 0) {
      return;
    }
    char buf[512];
    (void)::recv(c, buf, sizeof(buf), 0);
    const char* payload = "{\"bpm\":128.0}";
    char resp[128];
    const int n = std::snprintf(
        resp, sizeof(resp),
        "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s",
        std::strlen(payload), payload);
    if (n > 0) {
      (void)::send(c, resp, static_cast<size_t>(n), 0);
    }
    ::close(c);
  });

  neon::client::PosixHttpTransport http;
  const auto r =
      http.request("GET", "127.0.0.1", port, "/api/status", nullptr, 2000);
  server.join();
  ::close(ls);

  CHECK(r.status == 200);
  CHECK(r.body == "{\"bpm\":128.0}");
  CHECK_FALSE(r.timed_out);
  CHECK_FALSE(r.connect_failed);
}

TEST_CASE("PosixHttpTransport times out when the server never replies") {
  int ls = ::socket(AF_INET, SOCK_STREAM, 0);
  REQUIRE(ls >= 0);
  int yes = 1;
  setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  REQUIRE(::bind(ls, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
  REQUIRE(::listen(ls, 1) == 0);
  socklen_t alen = sizeof(addr);
  REQUIRE(getsockname(ls, reinterpret_cast<sockaddr*>(&addr), &alen) == 0);
  const int port = ntohs(addr.sin_port);

  std::thread server([&] {
    int c = ::accept(ls, nullptr, nullptr);
    if (c < 0) {
      return;
    }
    char buf[512];
    (void)::recv(c, buf, sizeof(buf), 0);
    // Hold the connection; do not write a response.
    sleep(2);
    ::close(c);
  });

  neon::client::PosixHttpTransport http;
  const auto start = std::chrono::steady_clock::now();
  const auto r =
      http.request("PUT", "127.0.0.1", port, "/api/config", "{}", 200);
  const auto elapsed = std::chrono::steady_clock::now() - start;
  server.join();
  ::close(ls);

  CHECK(r.timed_out);
  CHECK(elapsed < std::chrono::milliseconds(800));
}
#endif
