#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Form.h"
#include "HeroTempo.h"
#include "PhaseBar.h"
#include "../DeviceController.h"
#include "neon/client/sync.hpp"
#include "neon/config/model.hpp"

namespace neon::plugin {

struct EditorHost {
  std::function<void(std::function<void(neon::Config&)>)> patch;
  std::function<void(std::function<void(DeviceController&)>)> send;
};

class LivePage : public juce::Component {
 public:
  explicit LivePage(EditorHost&);
  void load(const Snapshot&, bool online);
  void setPhase(float phase01, int quantum, bool running);
  // The plugin's own Neon Sync peer — fed straight from the processor's
  // SyncService, independent of the REST snapshot above.
  void loadNeonSync(const neon::client::SyncStatus&, bool enabled);
  int preferredHeight() const { return pane_ == 0 ? 620 : 520; }
  void resized() override;
  void paint(juce::Graphics&) override;

  std::function<void(bool)> onSyncEnable;
  std::function<void(bool)> onSyncDrive;
  std::function<void()> onLayoutChange;  // pane switch changed the height

 private:
  EditorHost& host_;
  neon::ui::HeroTempo hero_;
  neon::ui::PhaseBar phase_;
  juce::Label beat_;
  juce::TextButton play_{"Play"};
  juce::TextButton tap_{"Tap"};
  juce::TextButton minus_{"-1"};
  juce::TextButton plus_{"+1"};
  juce::TextButton half_{"/2"};
  juce::TextButton double_{"x2"};
  juce::TextEditor bpm_;
  juce::TextButton setBpm_{"Set BPM"};
  neon::ui::SubNav extras_;
  juce::TextButton resyncNext_{"Reset next loop"};
  juce::TextButton resyncNow_{"Re-align grid now"};
  juce::Label nsHead_;
  neon::ui::Toggle nsEnable_;
  neon::ui::Toggle nsDrive_;
  neon::ui::Readout nsPeers_;
  neon::ui::Readout nsTempo_;
  neon::ui::Readout nsTransport_;
  neon::ui::Readout nsPhase_;
  juce::Label nsNote_;
  juce::TextButton save_[4];
  juce::TextButton recall_[4];
  juce::Label slotLab_[4];
  neon::ui::Readout edges_;
  neon::ui::Readout lateMax_;
  neon::ui::Readout lateAvg_;
  neon::ui::Readout uptime_;
  juce::Label presetMsg_;
  int pane_ = 0;
  bool show_beat_ = false;
};

class OutputsPage : public juce::Component {
 public:
  explicit OutputsPage(EditorHost&);
  void load(const neon::Config&);
  int preferredHeight() const;
  void resized() override;
  void paint(juce::Graphics&) override;

 private:
  enum Jack {
    SpdifIn,
    SpdifOut,
    LineIn,
    LineOut,
    MidiIn,
    MidiOut,
    Cv1In,
    Cv1Out,
    Cv2In,
    Cv2Out
  };

  void selectJack(Jack);
  void bindClock(int index, bool virt);
  static juce::String jackTitle(Jack);
  static juce::String jackTag(Jack, const neon::Config&);

  EditorHost& host_;
  neon::Config cfg_{};
  Jack jack_ = Cv2Out;
  int virt_ = 1;
  int clock_pane_ = 0;  // 0 shape, 1 groove
  bool editing_virt_ = false;

  juce::OwnedArray<juce::TextButton> holes_;
  juce::Label jackHead_;
  juce::Label jackCopy_;
  neon::ui::Toggle enabled_;
  neon::ui::SelectField role_;
  neon::ui::SubNav clockNav_;
  neon::ui::NumberField ppqn_;
  neon::ui::NumberField mult_;
  neon::ui::NumberField div_;
  neon::ui::SelectField mode_;
  neon::ui::NumberField trig_;
  neon::ui::NumberField duty_;
  neon::ui::Toggle freeRun_;
  neon::ui::NumberField shuffle_;
  neon::ui::SelectField rhythm_;
  neon::ui::NumberField steps_;
  neon::ui::NumberField fills_;
  neon::ui::NumberField rotate_;
  neon::ui::NumberField chance_;
  neon::ui::NumberField jitter_;
  neon::ui::Toggle overLoop_;
  neon::ui::StepGrid grid_;

  neon::ui::SelectField cv1Role_;
  neon::ui::NumberField cvMin_;
  neon::ui::NumberField cvMax_;
  neon::ui::SelectField clkSrc_;
  neon::ui::NumberField clkIn_;
  neon::ui::Toggle midiClkOut_;
  neon::ui::NumberField midiNudge_;
  neon::ui::SelectField midiPolicy_;
  neon::ui::Toggle midiTransport_;
  neon::ui::Toggle audioEn_;
  neon::ui::SelectField roleL_;
  neon::ui::SelectField roleR_;
  neon::ui::NumberField lineMon_;

  juce::Label virtHead_;
  neon::ui::SubNav virtNav_;
};

class NetworkPage : public juce::Component {
 public:
  explicit NetworkPage(EditorHost&);
  void load(const neon::Config&, const Snapshot&);
  void tickStatus(const Snapshot&);
  int preferredHeight() const;
  void resized() override;

 private:
  EditorHost& host_;
  neon::Config cfg_{};
  neon::client::ConfigSecrets secrets_{};
  Snapshot last_snap_{};
  int pane_ = 0;
  int slot_ = 0;

  neon::ui::SubNav nav_;
  neon::ui::Readout addr_;
  neon::ui::Readout hostname_;
  neon::ui::Readout trying_;
  neon::ui::Readout ap_;
  neon::ui::Readout peers_;
  juce::Label netChip_;

  neon::ui::SubNav slots_;
  neon::ui::TextField ssid_;
  neon::ui::TextField pass_;
  neon::ui::NumberField retries_;
  neon::ui::Toggle hidden_;
  juce::TextButton scan_{"Scan"};
  juce::Label scanMsg_;
  juce::OwnedArray<juce::TextButton> found_;

  neon::ui::SelectField policy_;
  neon::ui::TextField apSsid_;
  neon::ui::TextField apPass_;
  neon::ui::NumberField channel_;
  neon::ui::Toggle requirePass_;
  neon::ui::Toggle apHidden_;
};

class MidiPage : public juce::Component {
 public:
  explicit MidiPage(EditorHost&);
  void load(const neon::Config&);
  int preferredHeight() const;
  void resized() override;

 private:
  EditorHost& host_;
  int pane_ = 0;
  neon::ui::SubNav nav_;
  juce::Label bleChip_;
  neon::ui::Toggle bleEn_;
  neon::ui::Toggle clkOut_;
  neon::ui::Toggle transport_;
  neon::ui::SelectField channel_;
  neon::ui::SelectField gate_;
  neon::ui::SelectField policy_;
  neon::ui::Toggle pitch_;
  neon::ui::NumberField ccLat_;
  neon::ui::NumberField ccShuf_;
};

class AudioPage : public juce::Component {
 public:
  explicit AudioPage(EditorHost&);
  void load(const neon::Config&, const Snapshot&);
  void tickStatus(const Snapshot&);
  int preferredHeight() const;
  void resized() override;

 private:
  EditorHost& host_;
  neon::Config cfg_{};
  int pane_ = 0;
  neon::ui::SubNav nav_;
  juce::Label banner_;
  neon::ui::Toggle enabled_;
  neon::ui::SelectField roleL_;
  neon::ui::SelectField roleR_;
  neon::ui::Meter peakL_;
  neon::ui::Meter peakR_;
  neon::ui::Toggle metroEn_;
  neon::ui::SelectField metroSnd_;
  neon::ui::NumberField metroGain_;
  neon::ui::Toggle metroAcc_;
  neon::ui::Toggle amyEn_;
  neon::ui::NumberField amyPatch_;
  neon::ui::NumberField amyGain_;
  neon::ui::NumberField lineMon_;
  neon::ui::Toggle followEn_;
  neon::ui::NumberField followSens_;
  neon::ui::Toggle followPhase_;
  neon::ui::SelectField followIn_;
  juce::Label followNote_;
  neon::ui::Toggle pubMix_;
  neon::ui::Toggle pubLine_;
  neon::ui::Toggle pubMono_;
  neon::ui::TextField chName_;
  juce::Label pubNote_;
  neon::ui::SelectField subCh_;
  neon::ui::NumberField jitter_;
  neon::ui::NumberField subGain_;
  juce::TextButton refresh_{"Refresh"};
  juce::Label subNote_;
};

class SystemPage : public juce::Component {
 public:
  explicit SystemPage(EditorHost&);
  void load(const neon::Config&, const Snapshot&);
  int preferredHeight() const;
  void resized() override;

  std::function<void()> onReboot;
  std::function<void()> onFactoryReset;

 private:
  EditorHost& host_;
  int pane_ = 0;
  neon::ui::SubNav nav_;
  neon::ui::NumberField latency_;
  neon::ui::NumberField quantum_;
  neon::ui::SelectField resetMode_;
  neon::ui::NumberField resetLen_;
  neon::ui::NumberField midiNudge_;
  neon::ui::NumberField resetLead_;
  neon::ui::Toggle gating_;
  neon::ui::Toggle resetLeadEn_;
  neon::ui::Toggle startStop_;
  neon::ui::SelectField clkSrc_;
  neon::ui::NumberField clkIn_;
  neon::ui::NumberField cvMin_;
  neon::ui::NumberField cvMax_;
  neon::ui::TextField name_;
  neon::ui::NumberField bright_;
  neon::ui::Toggle bigBeat_;
  neon::ui::SelectField beatStyle_;
  neon::ui::Readout fw_;
  neon::ui::Readout hostname_;
  neon::ui::Readout ip_;
  juce::TextButton reboot_{"Reboot"};
  juce::TextButton factory_{"Factory reset"};
};

}  // namespace neon::plugin
