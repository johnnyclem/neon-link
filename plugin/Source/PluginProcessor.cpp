#include "PluginProcessor.h"
#include "PluginEditor.h"

NeonLinkProcessor::NeonLinkProcessor()
    : juce::AudioProcessor(
          BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
              .withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
  // Scan-safe: no sockets, no threads, no mDNS.
}

NeonLinkProcessor::~NeonLinkProcessor() {
  stopTimer();
  if (controller_) controller_->requestStop();
}

void NeonLinkProcessor::ensureControllerStarted() {
  if (controller_ != nullptr) return;
  stopTimer();
  controller_ = std::make_unique<neon::plugin::DeviceController>();
  if (pending_ip_.isNotEmpty()) {
    controller_->hintIp(pending_ip_.toStdString());
  }
  if (pending_host_.isNotEmpty()) {
    controller_->bind(pending_host_.toStdString());
  }
  controller_->startThread();
}

void NeonLinkProcessor::timerCallback() {
  stopTimer();
  ensureControllerStarted();
}

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
  ensureControllerStarted();
}

void NeonLinkProcessor::releaseResources() {
  // Live fires this on sample-rate changes. Do not tear down HTTP.
}

void NeonLinkProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                     juce::MidiBuffer& midi) {
  juce::ScopedNoDenormals noDenormals;
  midi.clear();
  juce::ignoreUnused(buffer);
}

void NeonLinkProcessor::getStateInformation(juce::MemoryBlock& dest) {
  auto xml = std::make_unique<juce::XmlElement>("NEONLINK");
  xml->setAttribute("state_version", 1);
  juce::String host = "neon-link.local";
  juce::String ip;
  if (controller_) {
    const auto b = controller_->bind_state();
    if (!b.connect_host.empty()) host = b.connect_host;
    if (!b.ip.empty()) ip = b.ip;
  } else {
    if (pending_host_.isNotEmpty()) host = pending_host_;
    ip = pending_ip_;
  }
  xml->setAttribute("host", host);
  if (ip.isNotEmpty()) xml->setAttribute("ip", ip);
  copyXmlToBinary(*xml, dest);
}

void NeonLinkProcessor::setStateInformation(const void* data, int size) {
  auto xml = getXmlFromBinary(data, size);
  if (xml == nullptr || !xml->hasTagName("NEONLINK")) return;
  pending_host_ = xml->getStringAttribute("host", "neon-link.local");
  pending_ip_ = xml->getStringAttribute("ip");
  if (controller_) {
    if (pending_ip_.isNotEmpty()) controller_->hintIp(pending_ip_.toStdString());
    controller_->bind(pending_host_.toStdString());
  }
}

juce::AudioProcessorEditor* NeonLinkProcessor::createEditor() {
  ensureControllerStarted();
  return new NeonLinkEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
  return new NeonLinkProcessor();
}
