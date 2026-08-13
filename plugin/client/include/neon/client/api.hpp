#pragma once

#include <string>

#include "neon/client/http.hpp"
#include "neon/client/patch.hpp"
#include "neon/client/status.hpp"
#include "neon/config/model.hpp"

namespace neon::client {

template <typename T>
struct Result {
  bool ok = false;
  int http_status = 0;  // 0 if no HTTP response
  bool timed_out = false;
  bool connect_failed = false;
  std::string error;
  T value{};
};

template <>
struct Result<void> {
  bool ok = false;
  int http_status = 0;
  bool timed_out = false;
  bool connect_failed = false;
  std::string error;
};

enum class TransportOp { Play, Stop, Toggle, PlayNow, StopNow };
enum class TempoOp { Tap, Double, Half, Nudge };
enum class ResyncOp { Next, Now };
enum class PresetOp { Save, Recall };
enum class Persist { Now, Lazy };

class DeviceClient {
 public:
  explicit DeviceClient(HttpTransport& http);

  void setTarget(const char* host, int port = 80);

  // Timeouts (tighter than the web editor's implicit ~10 s fetch):
  //   status  800 ms
  //   config 1500 ms
  //   cmd     800 ms
  //   scan   8000 ms
  Result<Status> getStatus();
  Result<neon::Config> getConfig();

  // Persist::Lazy is sent only when the last getStatus advertised
  // persist_lazy. Otherwise the call fails without touching the network
  // (pre-F5 firmware ignores the query and immediate-saves NVS).
  Result<neon::Config> putConfig(const JsonPatch& patch,
                                Persist persist = Persist::Now);

  bool persist_lazy() const { return persist_lazy_; }

  Result<void> transport(TransportOp);
  Result<void> setTempo(double bpm);
  Result<void> tempoOp(TempoOp, int delta = 1);
  Result<void> resync(ResyncOp);
  Result<void> preset(PresetOp, int slot /*0..3*/);
  Result<std::vector<ScanResult>> scan();
  Result<AudioChannels> audioChannels();
  Result<void> reboot();  // fire-and-forget; connection drop is success

 private:
  HttpTransport& http_;
  std::string host_ = "neon-link.local";
  int port_ = 80;
  bool persist_lazy_ = false;

  template <typename T>
  Result<T> fill_error(const HttpResponse& r, const char* what) const;

  Result<void> cmd(const char* path, int timeout_ms);
};

}  // namespace neon::client
