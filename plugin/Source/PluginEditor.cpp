#include "PluginEditor.h"

namespace {
constexpr uint32_t kBg = 0xff0b0c0f;  // DESIGN_SYSTEM.md --bg
}

NeonLinkEditor::NeonLinkEditor(NeonLinkProcessor& p)
    : juce::AudioProcessorEditor(&p), processor_(p) {
  setSize(520, 360);
  setResizable(true, false);
  setResizeLimits(360, 240, 1200, 900);
}

NeonLinkEditor::~NeonLinkEditor() = default;

void NeonLinkEditor::paint(juce::Graphics& g) {
  g.fillAll(juce::Colour(kBg));
  g.setColour(juce::Colour(0xff8b909a));  // --text-muted
  g.setFont(14.0f);
  g.drawFittedText(processor_.getName(), getLocalBounds(),
                   juce::Justification::centred, 1);
}

void NeonLinkEditor::resized() {}
