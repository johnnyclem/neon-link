#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace neon::ui {

// Same seven-segment map as design/fonts/hero.json.
class HeroTempo : public juce::Component {
 public:
  void setBpm(double bpm, bool valid);
  void paint(juce::Graphics&) override;

 private:
  double bpm_ = 0;
  bool valid_ = false;
};

}  // namespace neon::ui
