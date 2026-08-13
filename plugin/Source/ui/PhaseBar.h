#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace neon::ui {

// Device coordinate space: 128 wide, same inset/ticks as the panel.
class PhaseBar : public juce::Component {
 public:
  void setPhase(float phase01, int quantum, bool running);
  void paint(juce::Graphics&) override;

 private:
  float phase_ = 0;
  int quantum_ = 4;
  bool running_ = false;
};

}  // namespace neon::ui
