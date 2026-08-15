#include "MicPage.h"

#include <cmath>
#include <string>

#include "Tokens.h"

namespace neon::plugin {
namespace {

double amp_to_db(double amp) {
  if (amp <= 1.0e-6) {
    return -60.0;
  }
  return juce::jlimit(-60.0, 0.0, 20.0 * std::log10(amp));
}

juce::String host_label(const MicSnapshot& snap) {
  if (!snap.bind.ip.empty() && snap.port != neon::client::kMicDefaultPort) {
    return juce::String(snap.bind.ip) + ":" + juce::String(snap.port);
  }
  if (!snap.bind.ip.empty()) {
    return snap.bind.ip;
  }
  if (snap.port != neon::client::kMicDefaultPort && !snap.bind.connect_host.empty()) {
    return juce::String(snap.bind.connect_host) + ":" + juce::String(snap.port);
  }
  if (!snap.bind.connect_host.empty()) {
    return snap.bind.connect_host;
  }
  return "neon-mic.local:17001";
}

const char* perm_label(neon::client::MicPermission p) {
  switch (p) {
    case neon::client::MicPermission::Granted:
      return "GRANT";
    case neon::client::MicPermission::Denied:
      return "DENY";
    case neon::client::MicPermission::Unknown:
    default:
      return "?";
  }
}

}  // namespace

void PeakRail::set(double rms, double peak, bool clip) {
  rms_ = juce::jlimit(0.0, 1.0, rms);
  peak_ = juce::jlimit(0.0, 1.0, peak);
  clip_ = clip;
  repaint();
}

void PeakRail::paint(juce::Graphics& g) {
  auto r = getLocalBounds();
  auto read = r.removeFromRight(72);
  g.setColour(clip_ ? neon::ui::danger() : neon::ui::muted());
  g.setFont(juce::Font(juce::FontOptions(11.0f)));
  g.drawText(clip_ ? "CLIP" : "PEAK", read.removeFromTop(14),
             juce::Justification::centredRight);
  const double pdb = amp_to_db(peak_);
  g.setColour(clip_ ? neon::ui::danger() : neon::ui::text());
  g.setFont(juce::Font(juce::FontOptions(18.0f, juce::Font::bold)));
  g.drawText(pdb <= -59.5 ? juce::String::fromUTF8("-\xe2\x88\x9e")
                          : juce::String(pdb, 1),
             read, juce::Justification::centredRight);

  r.removeFromRight(10);
  const int segs = 28;
  const int gap = 2;
  const int w = juce::jmax(2, (r.getWidth() - gap * (segs - 1)) / segs);
  const double rms_db = amp_to_db(rms_);
  const double peak_db = pdb;
  for (int i = 0; i < segs; ++i) {
    const double db = -60.0 + (static_cast<double>(i) / (segs - 1)) * 60.0;
    auto cell = juce::Rectangle<int>(r.getX() + i * (w + gap), r.getY() + 8, w,
                                     r.getHeight() - 16);
    const bool on = rms_db >= db;
    juce::Colour fill = neon::ui::surface2();
    if (on) {
      if (db >= -0.5) {
        fill = neon::ui::danger();
      } else if (db >= -6.0) {
        fill = neon::ui::yellow();
      } else {
        fill = neon::ui::neon();
      }
    }
    g.setColour(fill);
    g.fillRect(cell);
  }
  const float peak_x = static_cast<float>(
      r.getX() + juce::jlimit(0.0, 1.0, (peak_db + 60.0) / 60.0) * r.getWidth());
  g.setColour(neon::ui::text());
  g.fillRect(juce::Rectangle<float>(peak_x - 1.0f, static_cast<float>(r.getY() + 4),
                                    2.0f, static_cast<float>(r.getHeight() - 8)));
}

void PeakRail::mouseDown(const juce::MouseEvent&) {
  if (clip_ && onClearClip) {
    onClearClip();
  }
}

MicPage::MicPage(std::function<void(std::function<void(MicController&)>)> send)
    : send_(std::move(send)) {
  phoneLab_.setText("PHONE", juce::dontSendNotification);
  phoneLab_.setColour(juce::Label::textColourId, neon::ui::muted());
  phoneLab_.setFont(juce::Font(juce::FontOptions(11.0f)));
  addAndMakeVisible(phoneLab_);

  hostField_.setText("neon-mic.local:17001");
  hostField_.setTextToShowWhenEmpty("neon-mic.local:17001", neon::ui::muted());
  neon::ui::styleField(hostField_);
  hostField_.onReturnKey = [this] { bind_.triggerClick(); };
  addAndMakeVisible(hostField_);
  neon::ui::styleBtn(bind_, false);
  bind_.onClick = [this] {
    const auto typed = hostField_.getText().trim();
    send_([typed](MicController& c) { c.bind(typed.toStdString()); });
  };
  addAndMakeVisible(bind_);

  neon::ui::styleChip(chipLive_, neon::ui::muted(), neon::ui::surface());
  neon::ui::styleChip(chipRun_, neon::ui::muted(), neon::ui::surface());
  neon::ui::styleChip(chipSub_, neon::ui::magenta(), neon::ui::surface());
  neon::ui::styleChip(chipPerm_, neon::ui::muted(), neon::ui::surface());
  addAndMakeVisible(chipLive_);
  addAndMakeVisible(chipRun_);
  addAndMakeVisible(chipSub_);
  addAndMakeVisible(chipPerm_);

  meter_.onClearClip = [this] {
    if (!have_draft_) {
      return;
    }
    sendPatch([](neon::client::MicConfig&) {});
  };
  addAndMakeVisible(meter_);

  gainLab_.setText("GAIN", juce::dontSendNotification);
  gainLab_.setColour(juce::Label::textColourId, neon::ui::muted());
  gainLab_.setFont(juce::Font(juce::FontOptions(11.0f)));
  addAndMakeVisible(gainLab_);
  gainVal_.setColour(juce::Label::textColourId, neon::ui::text());
  gainVal_.setFont(juce::Font(juce::FontOptions(13.0f)));
  gainVal_.setJustificationType(juce::Justification::centredRight);
  addAndMakeVisible(gainVal_);
  gain_.setSliderStyle(juce::Slider::LinearHorizontal);
  gain_.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
  gain_.setRange(-24.0, 36.0, 0.5);
  gain_.setValue(0.0, juce::dontSendNotification);
  gain_.setColour(juce::Slider::backgroundColourId, neon::ui::surface2());
  gain_.setColour(juce::Slider::trackColourId, neon::ui::neon());
  gain_.setColour(juce::Slider::thumbColourId, neon::ui::text());
  gain_.onDragStart = [this] { gain_drag_ = true; };
  gain_.onValueChange = [this] {
    const double v = gain_.getValue();
    const juce::String txt =
        (v > 0 ? "+" : "") + juce::String(v, 1) + " dB";
    gainVal_.setText(txt, juce::dontSendNotification);
  };
  gain_.onDragEnd = [this] {
    gain_drag_ = false;
    commitGain();
  };
  addAndMakeVisible(gain_);

  peerLab_.setText("PEER NAME", juce::dontSendNotification);
  peerLab_.setColour(juce::Label::textColourId, neon::ui::muted());
  peerLab_.setFont(juce::Font(juce::FontOptions(11.0f)));
  addAndMakeVisible(peerLab_);
  neon::ui::styleField(peer_);
  peer_.setInputRestrictions(neon::client::kMicPeerNameMax);
  peer_.setText("iPhone");
  peer_.setTextToShowWhenEmpty("iPhone", neon::ui::muted());
  peer_.onReturnKey = [this] { commitPeer(); };
  peer_.onFocusLost = [this] { commitPeer(); };
  addAndMakeVisible(peer_);

  source_.set("Source", 1, {{1, "System default"}},
              "The chosen device is captured exactly. Audio never comes through this plugin.");
  addAndMakeVisible(source_);
  sourceHint_.setColour(juce::Label::textColourId, neon::ui::muted());
  sourceHint_.setFont(juce::Font(juce::FontOptions(11.0f)));
  addAndMakeVisible(sourceHint_);

  keepAwake_.setLabel("Hold the screen awake while live");
  keepAwake_.onChange = [this](bool v) {
    sendPatch([v](neon::client::MicConfig& c) { c.keep_alive = v; });
  };
  addAndMakeVisible(keepAwake_);
  followTransport_.setLabel("Publish only while the transport runs");
  followTransport_.onChange = [this](bool v) {
    sendPatch([v](neon::client::MicConfig& c) { c.start_stop_sync = v; });
  };
  addAndMakeVisible(followTransport_);
  publish_.setLabel("Publish the sink");
  publish_.onChange = [this](bool v) {
    sendPatch([v](neon::client::MicConfig& c) { c.publish = v; });
  };
  addAndMakeVisible(publish_);
  metronome_.setLabel("Local metronome");
  metronome_.onChange = [this](bool v) {
    sendPatch([v](neon::client::MicConfig& c) { c.metronome_enabled = v; });
  };
  addAndMakeVisible(keepAwake_);
  addAndMakeVisible(followTransport_);
  addAndMakeVisible(publish_);
  addAndMakeVisible(metronome_);

  readout_.setColour(juce::Label::textColourId, neon::ui::muted());
  readout_.setFont(juce::Font(juce::FontOptions(12.0f)));
  addAndMakeVisible(readout_);
  banner_.setColour(juce::Label::textColourId, neon::ui::yellow());
  banner_.setFont(juce::Font(juce::FontOptions(13.0f)));
  addAndMakeVisible(banner_);

  neon::ui::styleBtn(start_, true);
  start_.onClick = [this] {
    const auto op = streaming_ ? neon::client::CaptureOp::Stop
                               : neon::client::CaptureOp::Start;
    send_([op](MicController& c) { c.capture(op); });
  };
  addAndMakeVisible(start_);
}

void MicPage::sendPatch(std::function<void(neon::client::MicConfig&)> fn) {
  if (!have_draft_ || !online_) {
    return;
  }
  fn(draft_);
  neon::client::sanitize_mic_config(&draft_);
  const auto next = draft_;
  send_([next](MicController& c) { c.patch(next); });
}

void MicPage::commitPeer() {
  if (!have_draft_) {
    return;
  }
  const auto next = peer_.getText().trim().toStdString();
  if (next == draft_.peer_name) {
    return;
  }
  sendPatch([next](neon::client::MicConfig& c) { c.peer_name = next; });
}

void MicPage::commitGain() {
  if (!have_draft_) {
    return;
  }
  const double v = gain_.getValue();
  if (std::abs(v - draft_.gain_db) < 0.01) {
    return;
  }
  sendPatch([v](neon::client::MicConfig& c) { c.gain_db = v; });
}

void MicPage::load(const MicSnapshot& snap) {
  online_ = snap.reach == Reachability::Online;
  streaming_ = snap.status.streaming;
  if (snap.has_config) {
    draft_ = snap.config;
    have_draft_ = true;
  }

  if (!hostField_.hasKeyboardFocus(true)) {
    hostField_.setText(host_label(snap), juce::dontSendNotification);
  }

  const auto word = neon::client::presence_word(snap.status);
  if (const char* lab = reach_label(snap.reach)) {
    chipLive_.setText(lab, juce::dontSendNotification);
    neon::ui::styleChip(chipLive_,
                        snap.reach == Reachability::Offline ? neon::ui::danger()
                                                            : neon::ui::yellow(),
                        neon::ui::surface());
  } else if (word == neon::client::PresenceWord::Live) {
    chipLive_.setText("LIVE", juce::dontSendNotification);
    neon::ui::styleChip(chipLive_, neon::ui::success(), neon::ui::surface());
  } else if (word == neon::client::PresenceWord::Local) {
    chipLive_.setText("LOCAL", juce::dontSendNotification);
    neon::ui::styleChip(chipLive_, neon::ui::neon(), neon::ui::surface());
  } else {
    chipLive_.setText("IDLE", juce::dontSendNotification);
    neon::ui::styleChip(chipLive_, neon::ui::muted(), neon::ui::surface());
  }

  chipRun_.setText(snap.status.playing ? "RUN" : "STOP", juce::dontSendNotification);
  neon::ui::styleChip(chipRun_,
                      snap.status.playing ? neon::ui::success() : neon::ui::muted(),
                      neon::ui::surface());
  chipRun_.setVisible(online_);

  if (snap.status.subscribers > 0) {
    chipSub_.setText(juce::String(static_cast<int>(snap.status.subscribers)) + " SUB",
                     juce::dontSendNotification);
    chipSub_.setVisible(online_);
    neon::ui::styleChip(chipSub_, neon::ui::magenta(), neon::ui::surface());
  } else {
    chipSub_.setVisible(false);
  }

  chipPerm_.setText(perm_label(snap.status.permission), juce::dontSendNotification);
  neon::ui::styleChip(chipPerm_,
                      snap.status.permission == neon::client::MicPermission::Denied
                          ? neon::ui::danger()
                          : neon::ui::muted(),
                      neon::ui::surface());
  chipPerm_.setVisible(online_);

  meter_.set(snap.status.rms, snap.status.peak, snap.status.clip);

  if (!gain_drag_) {
    gain_.setValue(draft_.gain_db, juce::dontSendNotification);
    const double v = draft_.gain_db;
    gainVal_.setText((v > 0 ? "+" : "") + juce::String(v, 1) + " dB",
                     juce::dontSendNotification);
  }
  gain_.setEnabled(online_);

  if (!peer_.hasKeyboardFocus(true)) {
    peer_.setText(draft_.peer_name, juce::dontSendNotification);
  }

  std::string source_sig = draft_.source_id + "|";
  source_sig += std::to_string(snap.config_seq);
  for (const auto& row : snap.sources) {
    source_sig += "\n";
    source_sig += row.id;
    source_sig += "=";
    source_sig += row.label;
  }
  source_sig += draft_.keep_alive ? "1" : "0";
  source_sig += draft_.start_stop_sync ? "1" : "0";
  source_sig += draft_.publish ? "1" : "0";
  source_sig += draft_.metronome_enabled ? "1" : "0";
  const bool sources_changed = source_sig != last_source_sig_;
  if (sources_changed) {
    last_source_sig_ = source_sig;
    last_config_seq_ = snap.config_seq;
    std::vector<neon::ui::Option> opts;
    opts.push_back({1, "System default"});
    int selected = 1;
    bool unlabeled = !snap.sources.empty();
    for (size_t i = 0; i < snap.sources.size(); ++i) {
      const auto& row = snap.sources[i];
      const int id = static_cast<int>(i) + 2;
      juce::String lab = row.label.empty()
                             ? ("Microphone " + juce::String(static_cast<int>(i) + 1))
                             : juce::String(row.label);
      opts.push_back({id, lab});
      if (!row.label.empty()) {
        unlabeled = false;
      }
      if (row.id == draft_.source_id && !draft_.source_id.empty()) {
        selected = id;
      }
    }
    source_.set("Source", selected, opts);
    source_.onChange = [this, sources = snap.sources](int id) {
      sendPatch([id, sources](neon::client::MicConfig& c) {
        if (id <= 1) {
          c.source_id.clear();
          return;
        }
        const int idx = id - 2;
        if (idx >= 0 && idx < static_cast<int>(sources.size())) {
          c.source_id = sources[static_cast<size_t>(idx)].id;
        }
      });
    };
    sourceHint_.setText(
        unlabeled ? "Names appear after the phone grants mic permission."
                  : "The chosen device is captured exactly. Audio never comes through this plugin.",
        juce::dontSendNotification);

    keepAwake_.setValue(draft_.keep_alive);
    followTransport_.setValue(draft_.start_stop_sync);
    publish_.setValue(draft_.publish);
    metronome_.setValue(draft_.metronome_enabled);
  }

  if (online_) {
    const double rate = snap.status.sample_rate / 1000.0;
    readout_.setText(juce::String(snap.status.latency_ms, 1) + " ms  ·  " +
                         juce::String(rate, 1) + " kHz mono  ·  fw " +
                         juce::String(snap.status.firmware),
                     juce::dontSendNotification);
  } else {
    readout_.setText("Bind a phone to see rate and latency.",
                     juce::dontSendNotification);
  }

  banner_.setText(snap.banner, juce::dontSendNotification);
  banner_.setColour(juce::Label::textColourId,
                    snap.kind_mismatch || !snap.status.error.empty()
                        ? neon::ui::danger()
                        : neon::ui::yellow());

  start_.setButtonText(streaming_ ? "Stop" : "Start");
  start_.setEnabled(online_ && !snap.capturing);
  if (streaming_) {
    neon::ui::styleDanger(start_);
  } else {
    neon::ui::styleBtn(start_, true);
  }
}

void MicPage::resized() {
  neon::ui::Stack s{getLocalBounds()};
  auto bindRow = s.next(32);
  phoneLab_.setBounds(bindRow.removeFromLeft(56));
  bind_.setBounds(bindRow.removeFromRight(72));
  bindRow.removeFromRight(8);
  hostField_.setBounds(bindRow);

  auto chips = s.next(24);
  chipLive_.setBounds(chips.removeFromLeft(92).reduced(0, 1));
  chips.removeFromLeft(6);
  chipRun_.setBounds(chips.removeFromLeft(56).reduced(0, 1));
  chips.removeFromLeft(6);
  chipSub_.setBounds(chips.removeFromLeft(64).reduced(0, 1));
  chips.removeFromLeft(6);
  chipPerm_.setBounds(chips.removeFromLeft(64).reduced(0, 1));

  meter_.setBounds(s.next(64));

  auto gainHead = s.next(16);
  gainLab_.setBounds(gainHead.removeFromLeft(48));
  gainVal_.setBounds(gainHead.removeFromRight(80));
  gain_.setBounds(s.next(28));

  auto peerHead = s.next(14);
  peerLab_.setBounds(peerHead);
  peer_.setBounds(s.next(28));
  source_.setBounds(s.next(56));
  sourceHint_.setBounds(s.next(16));

  keepAwake_.setBounds(s.next(28));
  followTransport_.setBounds(s.next(28));
  publish_.setBounds(s.next(28));
  metronome_.setBounds(s.next(28));

  readout_.setBounds(s.next(18));
  banner_.setBounds(s.next(22));
  start_.setBounds(s.next(40));
}

}  // namespace neon::plugin
