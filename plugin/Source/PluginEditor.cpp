#include "PluginEditor.h"
#include "ui/Tokens.h"

#include <cmath>

namespace {
void styleChip(juce::Label& l, juce::Colour fg, juce::Colour bg) {
  l.setColour(juce::Label::textColourId, fg);
  l.setColour(juce::Label::backgroundColourId, bg);
  l.setJustificationType(juce::Justification::centred);
  l.setFont(juce::Font(juce::FontOptions(11.0f)).withExtraKerningFactor(0.12f));
}

void styleBtn(juce::TextButton& b, bool primary) {
  b.setColour(juce::TextButton::buttonColourId,
              primary ? neon::ui::neon() : neon::ui::surface2());
  b.setColour(juce::TextButton::textColourOffId,
              primary ? neon::ui::bg() : neon::ui::text());
  b.setColour(juce::TextButton::buttonOnColourId, neon::ui::neonDim());
}

void styleField(juce::TextEditor& e) {
  e.setColour(juce::TextEditor::backgroundColourId, neon::ui::surface());
  e.setColour(juce::TextEditor::textColourId, neon::ui::text());
  e.setColour(juce::TextEditor::outlineColourId, neon::ui::border());
  e.setColour(juce::TextEditor::focusedOutlineColourId, neon::ui::neon());
  e.setColour(juce::TextEditor::highlightColourId, neon::ui::neonDim());
  e.setFont(juce::Font(juce::FontOptions(14.0f)));
}
}  // namespace

NeonLinkEditor::NeonLinkEditor(NeonLinkProcessor& p)
    : juce::AudioProcessorEditor(&p), processor_(p) {
  setSize(560, 620);
  setResizable(true, false);
  setResizeLimits(440, 520, 900, 900);

  brand_.setText("NEON  LINK", juce::dontSendNotification);
  brand_.setColour(juce::Label::textColourId, neon::ui::text());
  brand_.setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold))
                     .withExtraKerningFactor(0.14f));
  addAndMakeVisible(brand_);

  styleChip(reach_, neon::ui::danger(), neon::ui::surface());
  styleChip(chipSource_, neon::ui::neon(), neon::ui::surface());
  styleChip(chipTransport_, neon::ui::text(), neon::ui::surface());
  styleChip(chipNet_, neon::ui::success(), neon::ui::surface());
  addAndMakeVisible(reach_);
  addAndMakeVisible(chipSource_);
  addAndMakeVisible(chipTransport_);
  addAndMakeVisible(chipNet_);

  host_.setText("neon-link.local");
  host_.setTextToShowWhenEmpty("neon-link.local or 10.0.0.42",
                               neon::ui::muted());
  styleField(host_);
  addAndMakeVisible(host_);
  styleBtn(bind_, false);
  bind_.onClick = [this] {
    send([&](neon::plugin::DeviceController& c) {
      c.bind(host_.getText().toStdString());
    });
  };
  addAndMakeVisible(bind_);

  addAndMakeVisible(hero_);
  addAndMakeVisible(phase_);

  auto hook = [this](juce::TextButton& b, auto fn) {
    styleBtn(b, false);
    b.onClick = [this, fn] { send(fn); };
    addAndMakeVisible(b);
  };
  styleBtn(play_, true);
  play_.onClick = [this] {
    send([](neon::plugin::DeviceController& c) {
      c.transport(neon::client::TransportOp::Toggle);
    });
  };
  addAndMakeVisible(play_);
  hook(tap_, [](neon::plugin::DeviceController& c) {
    c.tempoOp(neon::client::TempoOp::Tap);
  });
  hook(minus_, [](neon::plugin::DeviceController& c) {
    c.tempoOp(neon::client::TempoOp::Nudge, -1);
  });
  hook(plus_, [](neon::plugin::DeviceController& c) {
    c.tempoOp(neon::client::TempoOp::Nudge, 1);
  });
  hook(half_, [](neon::plugin::DeviceController& c) {
    c.tempoOp(neon::client::TempoOp::Half);
  });
  hook(double_, [](neon::plugin::DeviceController& c) {
    c.tempoOp(neon::client::TempoOp::Double);
  });

  bpm_.setInputRestrictions(7, "0123456789.");
  bpm_.setTextToShowWhenEmpty("120.0", neon::ui::muted());
  styleField(bpm_);
  addAndMakeVisible(bpm_);
  styleBtn(setBpm_, false);
  setBpm_.onClick = [this] {
    const double v = bpm_.getText().getDoubleValue();
    if (v >= 20.0 && v <= 999.0) {
      send([v](neon::plugin::DeviceController& c) { c.setTempo(v); });
      bpm_.clear();
    }
  };
  addAndMakeVisible(setBpm_);

  hook(resyncNext_, [](neon::plugin::DeviceController& c) {
    c.resync(neon::client::ResyncOp::Next);
  });
  hook(resyncNow_, [](neon::plugin::DeviceController& c) {
    c.resync(neon::client::ResyncOp::Now);
  });

  for (int i = 0; i < 4; ++i) {
    save_[i].setButtonText("Save " + juce::String(i + 1));
    recall_[i].setButtonText("Recall " + juce::String(i + 1));
    styleBtn(save_[i], false);
    styleBtn(recall_[i], false);
    save_[i].onClick = [this, i] {
      send([i](neon::plugin::DeviceController& c) {
        c.preset(neon::client::PresetOp::Save, i);
      });
    };
    recall_[i].onClick = [this, i] {
      send([i](neon::plugin::DeviceController& c) {
        c.preset(neon::client::PresetOp::Recall, i);
      });
    };
    addAndMakeVisible(save_[i]);
    addAndMakeVisible(recall_[i]);
  }

  banner_.setColour(juce::Label::textColourId, neon::ui::yellow());
  banner_.setFont(juce::Font(juce::FontOptions(13.0f)));
  addAndMakeVisible(banner_);
  stats_.setColour(juce::Label::textColourId, neon::ui::muted());
  stats_.setFont(juce::Font(juce::FontOptions(12.0f)));
  addAndMakeVisible(stats_);

  if (auto* c = processor_.controller()) {
    c->setEditorOpen(true);
    const auto b = c->bind_state();
    if (!b.connect_host.empty()) host_.setText(b.connect_host);
  }

  last_tick_ms_ = juce::Time::getMillisecondCounterHiRes();
  startTimerHz(24);
}

NeonLinkEditor::~NeonLinkEditor() {
  if (auto* c = processor_.controller()) c->setEditorOpen(false);
}

void NeonLinkEditor::send(
    std::function<void(neon::plugin::DeviceController&)> fn) {
  if (auto* c = processor_.controller()) fn(*c);
}

void NeonLinkEditor::paint(juce::Graphics& g) {
  g.fillAll(neon::ui::bg());
  auto bounds = getLocalBounds().reduced(16);
  bounds.removeFromTop(92);
  auto card = bounds.removeFromTop(220);
  g.setColour(neon::ui::surface());
  g.fillRect(card);
  g.setColour(neon::ui::border());
  g.drawRect(card, 1);
}

void NeonLinkEditor::resized() {
  auto r = getLocalBounds().reduced(16);
  auto top = r.removeFromTop(28);
  brand_.setBounds(top.removeFromLeft(160));
  reach_.setBounds(top.removeFromRight(110).reduced(2, 2));
  chipNet_.setBounds(top.removeFromRight(72).reduced(2, 2));
  chipTransport_.setBounds(top.removeFromRight(56).reduced(2, 2));
  chipSource_.setBounds(top.removeFromRight(56).reduced(2, 2));

  r.removeFromTop(10);
  auto bindRow = r.removeFromTop(32);
  bind_.setBounds(bindRow.removeFromRight(72));
  bindRow.removeFromRight(8);
  host_.setBounds(bindRow);

  r.removeFromTop(16);
  auto card = r.removeFromTop(220).reduced(12);
  hero_.setBounds(card.removeFromTop(72));
  card.removeFromTop(8);
  phase_.setBounds(card.removeFromTop(40));
  card.removeFromTop(10);
  auto row = card.removeFromTop(32);
  const int gap = 6;
  auto slice = [&](int n) {
    const int w = (row.getWidth() - gap * (n - 1)) / n;
    auto b = row.removeFromLeft(w);
    row.removeFromLeft(gap);
    return b;
  };
  play_.setBounds(slice(6));
  tap_.setBounds(slice(6));
  minus_.setBounds(slice(6));
  plus_.setBounds(slice(6));
  half_.setBounds(slice(6));
  double_.setBounds(row);

  r.removeFromTop(12);
  auto tempo = r.removeFromTop(32);
  setBpm_.setBounds(tempo.removeFromRight(96));
  tempo.removeFromRight(8);
  bpm_.setBounds(tempo);

  r.removeFromTop(10);
  auto sync = r.removeFromTop(32);
  resyncNext_.setBounds(sync.removeFromLeft(sync.getWidth() / 2 - 4));
  sync.removeFromLeft(8);
  resyncNow_.setBounds(sync);

  r.removeFromTop(12);
  for (int i = 0; i < 4; ++i) {
    auto pr = r.removeFromTop(28);
    save_[i].setBounds(pr.removeFromLeft(pr.getWidth() / 2 - 4));
    pr.removeFromLeft(8);
    recall_[i].setBounds(pr);
    r.removeFromTop(6);
  }

  banner_.setBounds(r.removeFromTop(24));
  stats_.setBounds(r.removeFromTop(22));
}

void NeonLinkEditor::timerCallback() {
  refreshFromSnapshot();
}

void NeonLinkEditor::refreshFromSnapshot() {
  auto* ctl = processor_.controller();
  if (ctl == nullptr) return;
  const auto snap = ctl->snapshot();
  if (!snap) return;

  const bool online = snap->reach == neon::plugin::Reachability::Online;
  const auto& st = snap->status;

  if (const char* lab = neon::plugin::reach_label(snap->reach)) {
    reach_.setText(lab, juce::dontSendNotification);
    reach_.setVisible(true);
    styleChip(reach_,
              snap->reach == neon::plugin::Reachability::Offline
                  ? neon::ui::danger()
                  : neon::ui::yellow(),
              neon::ui::surface());
    chipSource_.setVisible(false);
    chipTransport_.setVisible(false);
    chipNet_.setVisible(false);
  } else {
    reach_.setVisible(false);
    chipSource_.setVisible(true);
    chipTransport_.setVisible(true);
    chipNet_.setVisible(true);
    chipSource_.setText(st.ext_clock ? "EXT" : "LINK",
                        juce::dontSendNotification);
    styleChip(chipSource_, neon::ui::neon(), neon::ui::surface());
    chipTransport_.setText(st.playing ? "RUN" : "STOP",
                           juce::dontSendNotification);
    styleChip(chipTransport_,
              st.playing ? neon::ui::success() : neon::ui::muted(),
              neon::ui::surface());
    juce::String net = "OFF";
    if (st.setup_ap)
      net = "AP";
    else if (st.network == neon::client::NetworkKind::Ethernet)
      net = "ETH";
    else if (st.network == neon::client::NetworkKind::Wifi)
      net = "STA";
    if (st.peers > 0) net += " " + juce::String(static_cast<int>(st.peers)) + "P";
    chipNet_.setText(net, juce::dontSendNotification);
    styleChip(chipNet_, neon::ui::success(), neon::ui::surface());
  }

  hero_.setBpm(st.bpm, online && st.tempo_valid);
  play_.setButtonText(st.playing ? "Stop" : "Play");
  styleBtn(play_, !st.playing);

  const double now = juce::Time::getMillisecondCounterHiRes();
  const double dt = juce::jlimit(0.0, 0.1, (now - last_tick_ms_) / 1000.0);
  last_tick_ms_ = now;

  const uint32_t q = st.quantum != 0 ? st.quantum : 4;
  if (!online || !st.playing) {
    phase_milli_ = st.phase_milli;
  } else {
    if (st.phase_milli != last_status_phase_) {
      phase_milli_ = st.phase_milli;
      last_status_phase_ = st.phase_milli;
    } else {
      phase_milli_ += st.bpm * 1000.0 / 60.0 * dt;
      const double span = static_cast<double>(q) * 1000.0;
      if (span > 0) {
        phase_milli_ = std::fmod(phase_milli_, span);
        if (phase_milli_ < 0) phase_milli_ += span;
      }
    }
  }
  playing_ = st.playing;
  const float ph =
      static_cast<float>(phase_milli_ / (static_cast<double>(q) * 1000.0));
  phase_.setPhase(ph, static_cast<int>(q), online && st.playing);

  banner_.setText(snap->banner, juce::dontSendNotification);
  if (online) {
    stats_.setText(
        "fw " + juce::String(st.firmware) + "   late " +
            juce::String(static_cast<int>(st.pulse.late_max_us)) + " µs   " +
            snap->bind.connect_host,
        juce::dontSendNotification);
  } else {
    stats_.setText("bind " + juce::String(snap->bind.connect_host),
                   juce::dontSendNotification);
  }
}
