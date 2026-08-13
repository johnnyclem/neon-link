#include "PluginProcessor.h"
#include "PluginEditor.h"

NeonLinkProcessor::NeonLinkProcessor()
    : juce::AudioProcessor(
          BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
              .withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
  // Scan-safe: no sockets, no threads, no mDNS.
}

NeonLinkProcessor::~NeonLinkProcessor() = default;

bool NeonLinkProcessor::isBusesLayoutSupported(
    const BusesLayout& layouts) const {
  const auto& out = layouts.getMainOutputChannelSet();
  const auto& in = layouts.getMainInputChannelSet();
  if (out != juce::AudioChannelSet::stereo() &&
      out != juce::AudioChannelSet::mono()) {
    return false;
  }
  return in == out;
}

void NeonLinkProcessor::prepareToPlay(double, int) {
  // DeviceController start is a later PR. This hook stays I/O-free until
  // then so a Live scan that calls prepareToPlay cannot open a socket.
}

void NeonLinkProcessor::releaseResources() {
  // Live fires this on sample-rate changes and engine stop. Do not tear
  // down network here when it exists.
}

void NeonLinkProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                     juce::MidiBuffer& midi) {
  juce::ScopedNoDenormals noDenormals;
  midi.clear();
  // Stereo passthrough. Live may deliver silence on an empty track.
  juce::ignoreUnused(buffer);
}

void NeonLinkProcessor::getStateInformation(juce::MemoryBlock&) {}

void NeonLinkProcessor::setStateInformation(const void*, int) {}

juce::AudioProcessorEditor* NeonLinkProcessor::createEditor() {
  return new NeonLinkEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
  return new NeonLinkProcessor();
}
