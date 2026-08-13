#pragma once

#include "PluginProcessor.h"

class NeonLinkEditor : public juce::AudioProcessorEditor {
 public:
  explicit NeonLinkEditor(NeonLinkProcessor&);
  ~NeonLinkEditor() override;

  void paint(juce::Graphics&) override;
  void resized() override;

 private:
  NeonLinkProcessor& processor_;
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NeonLinkEditor)
};
