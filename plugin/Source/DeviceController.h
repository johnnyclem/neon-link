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
// the editor is a view of Snapshot plus a local config draft.
class DeviceController : public juce::Thread {
 public:
  DeviceController();
  ~DeviceController() override;

  void run() override;
  void requestStop();

  void bind(const std::string& host_or_ip);
  // Last-good IPv4 from a previous session. Used so Bind on
  // neon-link.local does not have to win mDNS while Link Audio is up.
  void hintIp(const std::string& ip);
  void transport(neon::client::TransportOp);
  void tempoOp(neon::client::TempoOp, int delta = 1);
  void setTempo(double bpm);
  void resync(neon::client::ResyncOp);
  void preset(neon::client::PresetOp, int slot);

  void saveConfig(const neon::Config& cfg);
  void scanWifi();
  void refreshAudioChannels();
  void reboot();
  void factoryReset();

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
    Preset,
    SaveConfig,
    Scan,
    RefreshAudio,
    Reboot,
    FactoryReset
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
    neon::Config cfg{};
  };

  void post(Cmd c);
  void publish(Snapshot s);
  Snapshot base_snapshot() const;  // call with mu_ held
  void apply_bind(const std::string& raw);
  bool poll_once();
  bool fetch_config();
  void fill_status(Snapshot* s, const neon::client::Status& st);

  neon::client::PosixHttpTransport http_;
  neon::client::DeviceClient client_;

  mutable std::mutex mu_;
  std::vector<Cmd> queue_;
  Bind bind_;
  bool bound_by_ip_ = false;
  bool editor_open_ = false;

  neon::Config config_{};
  neon::client::ConfigSecrets secrets_{};
  bool have_config_ = false;
  uint32_t config_seq_ = 0;
  uint32_t last_status_rev_ = 0;

  bool saving_ = false;
  bool last_save_ok_ = false;
  std::string save_message_;
  uint32_t save_seq_ = 0;

  bool scanning_ = false;
  std::string scan_message_;
  std::vector<neon::client::ScanResult> scan_;

  neon::client::AudioChannels audio_channels_;
  bool audio_refreshing_ = false;

  std::shared_ptr<const Snapshot> snap_;
};

}  // namespace neon::plugin
