#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <juce_events/juce_events.h>

#include "MicSnapshot.h"
#include "neon/client/http.hpp"
#include "neon/client/mic.hpp"
#include "neon/client/mic_api.hpp"

namespace neon::plugin {

// Independent of DeviceController. Same HTTP rails, own host / poll / snapshot.
class MicController : public juce::Thread {
 public:
  MicController();
  ~MicController() override;

  void run() override;
  void requestStop();

  void bind(const std::string& host_or_ip);
  void hintIp(const std::string& ip);
  void patch(const neon::client::MicConfig& next);
  void capture(neon::client::CaptureOp);
  void refreshSources();
  void setEditorOpen(bool open);

  std::shared_ptr<const MicSnapshot> snapshot() const;
  Bind bind_state() const;
  int port() const;

 private:
  enum class Kind { Bind, Patch, Capture, RefreshSources };
  struct Cmd {
    Kind kind = Kind::Bind;
    std::string host;
    neon::client::MicConfig cfg{};
    neon::client::CaptureOp cop = neon::client::CaptureOp::Toggle;
  };

  void post(Cmd c);
  void publish(MicSnapshot s);
  MicSnapshot base_snapshot() const;  // call with mu_ held
  void apply_bind(const std::string& raw);
  bool poll_once();
  bool fetch_config();
  bool fetch_sources();
  void fill_status(MicSnapshot* s, const neon::client::MicStatus& st);

  neon::client::PosixHttpTransport http_;
  neon::client::MicClient client_;

  mutable std::mutex mu_;
  std::vector<Cmd> queue_;
  Bind bind_;
  int port_ = neon::client::kMicDefaultPort;
  bool bound_by_ip_ = false;
  bool editor_open_ = false;

  neon::client::MicConfig config_{};
  bool have_config_ = false;
  uint32_t config_seq_ = 0;
  uint32_t last_status_rev_ = 0;

  std::vector<neon::client::MicSourceRow> sources_;
  bool capturing_ = false;
  bool kind_mismatch_ = false;
  std::string banner_;

  std::shared_ptr<const MicSnapshot> snap_;
};

}  // namespace neon::plugin
