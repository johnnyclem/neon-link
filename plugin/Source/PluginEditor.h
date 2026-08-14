#pragma once

#include <functional>

#include "PluginProcessor.h"
#include "ui/Form.h"
#include "ui/Pages.h"

class NeonLinkEditor : public juce::AudioProcessorEditor, private juce::Timer {
 public:
  explicit NeonLinkEditor(NeonLinkProcessor&);
  ~NeonLinkEditor() override;

  void paint(juce::Graphics&) override;
  void resized() override;

 private:
  void timerCallback() override;
  void refreshFromSnapshot();
  void send(std::function<void(neon::plugin::DeviceController&)> fn);
  void patch(std::function<void(neon::Config&)> fn);
  void adoptConfig(const neon::plugin::Snapshot&);
  void doSave();
  void showTab(int index);
  void relayoutPage();
  void styleTabs();
  void showConfirm(const juce::String& title, const juce::String& body,
                   const juce::String& ok, std::function<void()> fn);

  NeonLinkProcessor& processor_;
  neon::ui::NeonLookAndFeel lnf_;

  neon::plugin::EditorHost host_;
  neon::Config draft_{};
  bool have_draft_ = false;
  bool dirty_ = false;
  uint32_t last_config_seq_ = 0;
  uint32_t last_save_seq_ = 0;

  juce::Label brand_;
  juce::Label vstVer_;
  juce::Label reach_;
  juce::Label chipSource_;
  juce::Label chipTransport_;
  juce::Label chipNet_;

  juce::TextEditor hostField_;
  juce::TextButton bind_{"Bind"};

  juce::TextButton tabLive_{"LIVE"};
  juce::TextButton tabOut_{"OUT"};
  juce::TextButton tabNet_{"NET"};
  juce::TextButton tabMidi_{"MIDI"};
  juce::TextButton tabAud_{"AUD"};
  juce::TextButton tabSys_{"SYS"};
  juce::TextButton* tabs_[6] = {&tabLive_, &tabOut_,  &tabNet_,
                                &tabMidi_, &tabAud_, &tabSys_};

  juce::Viewport viewport_;
  neon::plugin::LivePage live_;
  neon::plugin::OutputsPage outputs_;
  neon::plugin::NetworkPage network_;
  neon::plugin::MidiPage midi_;
  neon::plugin::AudioPage audio_;
  neon::plugin::SystemPage system_;
  int tab_ = 0;

  juce::TextButton save_{"Save"};
  juce::Label saveMsg_;
  juce::Label banner_;
  juce::Label stats_;

  neon::ui::Confirm confirm_;
  std::function<void()> confirm_fn_;

  double phase_milli_ = 0;
  uint32_t last_status_phase_ = 0;
  double last_tick_ms_ = 0;
  bool playing_ = false;
  double save_ok_until_ = 0;
  bool save_row_vis_ = false;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NeonLinkEditor)
};
