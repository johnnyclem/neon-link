#pragma once

#include <functional>

#include "PluginProcessor.h"
#include "ui/HeroTempo.h"
#include "ui/PhaseBar.h"

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

  NeonLinkProcessor& processor_;

  juce::Label brand_;
  juce::Label reach_;
  juce::Label chipSource_;
  juce::Label chipTransport_;
  juce::Label chipNet_;

  juce::TextEditor host_;
  juce::TextButton bind_{"Bind"};

  neon::ui::HeroTempo hero_;
  neon::ui::PhaseBar phase_;

  juce::TextButton play_{"Play"};
  juce::TextButton tap_{"Tap"};
  juce::TextButton minus_{"-1"};
  juce::TextButton plus_{"+1"};
  juce::TextButton half_{"÷2"};
  juce::TextButton double_{"×2"};

  juce::TextEditor bpm_;
  juce::TextButton setBpm_{"Set BPM"};

  juce::TextButton resyncNext_{"Reset next loop"};
  juce::TextButton resyncNow_{"Re-align grid now"};

  juce::TextButton save_[4];
  juce::TextButton recall_[4];

  juce::Label banner_;
  juce::Label stats_;

  double phase_milli_ = 0;
  uint32_t last_status_phase_ = 0;
  double last_tick_ms_ = 0;
  bool playing_ = false;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NeonLinkEditor)
};
