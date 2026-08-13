#pragma once

#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include "DeviceController.h"

class NeonLinkProcessor : public juce::AudioProcessor, private juce::Timer {
 public:
  NeonLinkProcessor();
  ~NeonLinkProcessor() override;

  void prepareToPlay(double sampleRate, int samplesPerBlock) override;
  void releaseResources() override;
  void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

  juce::AudioProcessorEditor* createEditor() override;
  bool hasEditor() const override { return true; }

  const juce::String getName() const override { return "NEON LINK"; }
  bool acceptsMidi() const override { return false; }
  bool producesMidi() const override { return false; }
  bool isMidiEffect() const override { return false; }
  double getTailLengthSeconds() const override { return 0.0; }

  int getNumPrograms() override { return 1; }
  int getCurrentProgram() override { return 0; }
  void setCurrentProgram(int) override {}
  const juce::String getProgramName(int) override { return {}; }
  void changeProgramName(int, const juce::String&) override {}

  void getStateInformation(juce::MemoryBlock& destData) override;
  void setStateInformation(const void* data, int sizeInBytes) override;

  bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

  void ensureControllerStarted();
  neon::plugin::DeviceController* controller() { return controller_.get(); }

 private:
  void timerCallback() override;

  std::unique_ptr<neon::plugin::DeviceController> controller_;
  juce::String pending_host_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NeonLinkProcessor)
};
