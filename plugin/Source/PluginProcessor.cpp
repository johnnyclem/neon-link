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
  if (mic_) mic_->requestStop();
  sync_.stop();
}

void NeonLinkProcessor::setSyncEnabled(bool enabled) {
  sync_enabled_ = enabled;
  if (enabled) {
    sync_.set_drive(sync_drive_);
    sync_.start();
  } else {
    sync_.stop();
  }
}

void NeonLinkProcessor::setSyncDrive(bool enabled) {
  sync_drive_ = enabled;
  sync_.set_drive(enabled);
}

void NeonLinkProcessor::ensureControllerStarted() {
  if (sync_enabled_ && !sync_.running()) {
    sync_.set_drive(sync_drive_);
    sync_.start();
  }
  if (controller_ != nullptr && mic_ != nullptr) return;
  stopTimer();
  if (controller_ == nullptr) {
    controller_ = std::make_unique<neon::plugin::DeviceController>();
    if (pending_ip_.isNotEmpty()) {
      controller_->hintIp(pending_ip_.toStdString());
    }
    if (pending_host_.isNotEmpty()) {
      controller_->bind(pending_host_.toStdString());
    }
    controller_->startThread();
  }
  if (mic_ == nullptr) {
    mic_ = std::make_unique<neon::plugin::MicController>();
    if (pending_mic_ip_.isNotEmpty()) {
      mic_->hintIp(pending_mic_ip_.toStdString());
    }
    if (pending_mic_host_.isNotEmpty()) {
      mic_->bind(pending_mic_host_.toStdString());
    }
    mic_->startThread();
  }
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

  // Hand the host's playhead to the Neon Sync bridge. Wait-free seqlock
  // publish — still zero locks and zero sockets on the audio callback.
  nsync::DawPlayhead ph;
  ph.sampled_us = neon::client::SyncService::now_us();
  if (auto* head = getPlayHead()) {
    if (const auto pos = head->getPosition()) {
      ph.valid = true;
      ph.playing = pos->getIsPlaying();
      ph.bpm = pos->getBpm().orFallback(0.0);
      ph.beat = pos->getPpqPosition().orFallback(0.0);
    }
  }
  sync_.publish_playhead(ph);
}

void NeonLinkProcessor::getStateInformation(juce::MemoryBlock& dest) {
  auto xml = std::make_unique<juce::XmlElement>("NEONLINK");
  xml->setAttribute("state_version", 2);
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

  juce::String mic_host = "neon-mic.local:17001";
  juce::String mic_ip;
  if (mic_) {
    const auto b = mic_->bind_state();
    const int port = mic_->port();
    if (!b.connect_host.empty()) {
      mic_host = b.connect_host;
      if (port > 0 && port != neon::client::kMicDefaultPort) {
        mic_host += ":" + juce::String(port);
      }
    }
    if (!b.ip.empty()) mic_ip = b.ip;
  } else {
    if (pending_mic_host_.isNotEmpty()) mic_host = pending_mic_host_;
    mic_ip = pending_mic_ip_;
  }
  xml->setAttribute("mic_host", mic_host);
  if (mic_ip.isNotEmpty()) xml->setAttribute("mic_ip", mic_ip);
  xml->setAttribute("sync_enabled", sync_enabled_);
  xml->setAttribute("sync_drive", sync_drive_);
  copyXmlToBinary(*xml, dest);
}

void NeonLinkProcessor::setStateInformation(const void* data, int size) {
  auto xml = getXmlFromBinary(data, size);
  if (xml == nullptr || !xml->hasTagName("NEONLINK")) return;
  pending_host_ = xml->getStringAttribute("host", "neon-link.local");
  pending_ip_ = xml->getStringAttribute("ip");
  pending_mic_host_ =
      xml->getStringAttribute("mic_host", "neon-mic.local:17001");
  pending_mic_ip_ = xml->getStringAttribute("mic_ip");
  sync_enabled_ = xml->getBoolAttribute("sync_enabled", true);
  sync_drive_ = xml->getBoolAttribute("sync_drive", true);
  sync_.set_drive(sync_drive_);
  // Session restore never *opens* sockets on its own — the service comes
  // up (per the restored flag) with the controllers — but an explicit
  // "off" must stick even if the service is already running.
  if (!sync_enabled_) sync_.stop();
  if (controller_) {
    if (pending_ip_.isNotEmpty()) controller_->hintIp(pending_ip_.toStdString());
    controller_->bind(pending_host_.toStdString());
  }
  if (mic_) {
    if (pending_mic_ip_.isNotEmpty()) mic_->hintIp(pending_mic_ip_.toStdString());
    mic_->bind(pending_mic_host_.toStdString());
  }
}

juce::AudioProcessorEditor* NeonLinkProcessor::createEditor() {
  ensureControllerStarted();
  return new NeonLinkEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
  return new NeonLinkProcessor();
}
