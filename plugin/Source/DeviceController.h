#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <juce_events/juce_events.h>

#include "Snapshot.h"
#include "neon/client/api.hpp"
#include "neon/client/http.hpp"

namespace neon::plugin {

// Network thread. Zero sockets on the audio callback. Processor owns this;
// the editor is a view of Snapshot.
class DeviceController : public juce::Thread {
 public:
  DeviceController();
  ~DeviceController() override;

  void run() override;
  void requestStop();

  void bind(const std::string& host_or_ip);
  void transport(neon::client::TransportOp);
  void tempoOp(neon::client::TempoOp, int delta = 1);
  void setTempo(double bpm);
  void resync(neon::client::ResyncOp);
  void preset(neon::client::PresetOp, int slot);

  void setEditorOpen(bool open);

  std::shared_ptr<const Snapshot> snapshot() const;

  Bind bind_state() const;

 private:
  enum class Kind {
    Bind,
    Transport,
    TempoOp,
    SetTempo,
    Resync,
    Preset
  };
  struct Cmd {
    Kind kind = Kind::Bind;
    std::string host;
    neon::client::TransportOp top = neon::client::TransportOp::Toggle;
    neon::client::TempoOp tempo = neon::client::TempoOp::Tap;
    neon::client::ResyncOp rop = neon::client::ResyncOp::Next;
    neon::client::PresetOp pop = neon::client::PresetOp::Recall;
    int slot = 0;
    int delta = 1;
    double bpm = 120;
  };

  void post(Cmd c);
  void publish(Snapshot s);
  void apply_bind(const std::string& raw);
  bool poll_once();

  neon::client::PosixHttpTransport http_;
  neon::client::DeviceClient client_;

  mutable std::mutex mu_;
  std::vector<Cmd> queue_;
  Bind bind_;
  bool bound_by_ip_ = false;
  bool editor_open_ = false;

  std::shared_ptr<const Snapshot> snap_;
};

}  // namespace neon::plugin
