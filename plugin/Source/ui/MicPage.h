#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Form.h"
#include "../MicController.h"

namespace neon::plugin {

class PeakRail : public juce::Component {
 public:
  std::function<void()> onClearClip;
  void set(double rms, double peak, bool clip);
  void paint(juce::Graphics&) override;
  void mouseDown(const juce::MouseEvent&) override;

 private:
  double rms_ = 0;
  double peak_ = 0;
  bool clip_ = false;
};

class MicPage : public juce::Component {
 public:
  explicit MicPage(std::function<void(std::function<void(MicController&)>)> send);
  void load(const MicSnapshot&);
  int preferredHeight() const { return 560; }
  void resized() override;

 private:
  void sendPatch(std::function<void(neon::client::MicConfig&)> fn);
  void commitPeer();
  void commitGain();

  std::function<void(std::function<void(MicController&)>)> send_;
  neon::client::MicConfig draft_{};
  bool have_draft_ = false;
  bool online_ = false;
  bool streaming_ = false;

  juce::Label phoneLab_;
  juce::TextEditor hostField_;
  juce::TextButton bind_{"Bind"};

  juce::Label chipLive_;
  juce::Label chipRun_;
  juce::Label chipSub_;
  juce::Label chipPerm_;

  PeakRail meter_;

  juce::Label gainLab_;
  juce::Label gainVal_;
  juce::Slider gain_;
  bool gain_drag_ = false;

  juce::Label peerLab_;
  juce::TextEditor peer_;
  neon::ui::SelectField source_;
  juce::Label sourceHint_;
  uint32_t last_config_seq_ = 0;
  std::string last_source_sig_;

  neon::ui::Toggle keepAwake_;
  neon::ui::Toggle followTransport_;
  neon::ui::Toggle publish_;
  neon::ui::Toggle metronome_;

  juce::Label readout_;
  juce::Label banner_;
  juce::TextButton start_{"Start"};
};

}  // namespace neon::plugin
