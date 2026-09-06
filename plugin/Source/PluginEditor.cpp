#include "PluginEditor.h"

#include <cmath>

#include "ui/Tokens.h"

NeonLinkEditor::NeonLinkEditor(NeonLinkProcessor& p)
    : juce::AudioProcessorEditor(&p),
      processor_(p),
      live_(host_),
      outputs_(host_),
      network_(host_),
      midi_(host_),
      audio_(host_),
      system_(host_),
      mic_([this](std::function<void(neon::plugin::MicController&)> fn) {
        sendMic(std::move(fn));
      }) {
  setLookAndFeel(&lnf_);
  setSize(700, 780);
  setResizable(true, false);
  setResizeLimits(540, 620, 1100, 1400);

  host_.patch = [this](std::function<void(neon::Config&)> fn) { patch(std::move(fn)); };
  host_.send = [this](std::function<void(neon::plugin::DeviceController&)> fn) {
    send(std::move(fn));
  };

  live_.onSyncEnable = [this](bool v) { processor_.setSyncEnabled(v); };
  live_.onSyncDrive = [this](bool v) { processor_.setSyncDrive(v); };
  live_.onLayoutChange = [this] { relayoutPage(); };

  brand_.setText("NEON  LINK", juce::dontSendNotification);
  brand_.setColour(juce::Label::textColourId, neon::ui::text());
  brand_.setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold))
                     .withExtraKerningFactor(0.14f));
  addAndMakeVisible(brand_);
  vstVer_.setText("vst " JucePlugin_VersionString, juce::dontSendNotification);
  vstVer_.setColour(juce::Label::textColourId, neon::ui::muted());
  vstVer_.setFont(juce::Font(juce::FontOptions(12.0f)));
  vstVer_.setJustificationType(juce::Justification::centredLeft);
  addAndMakeVisible(vstVer_);

  neon::ui::styleChip(reach_, neon::ui::danger(), neon::ui::surface());
  neon::ui::styleChip(chipSource_, neon::ui::neon(), neon::ui::surface());
  neon::ui::styleChip(chipTransport_, neon::ui::text(), neon::ui::surface());
  neon::ui::styleChip(chipNet_, neon::ui::success(), neon::ui::surface());
  addAndMakeVisible(reach_);
  addAndMakeVisible(chipSource_);
  addAndMakeVisible(chipTransport_);
  addAndMakeVisible(chipNet_);

  hostField_.setText("neon-link.local");
  hostField_.setTextToShowWhenEmpty("neon-link.local or 10.0.0.42", neon::ui::muted());
  neon::ui::styleField(hostField_);
  addAndMakeVisible(hostField_);
  neon::ui::styleBtn(bind_, false);
  bind_.onClick = [this] {
    processor_.ensureControllerStarted();
    const auto typed = hostField_.getText().trim();
    send([typed](neon::plugin::DeviceController& c) {
      c.bind(typed.toStdString());
    });
    have_draft_ = false;
    dirty_ = false;
  };
  addAndMakeVisible(bind_);

  for (int i = 0; i < 7; ++i) {
    addAndMakeVisible(*tabs_[i]);
    tabs_[i]->onClick = [this, i] { showTab(i); };
  }
  styleTabs();

  viewport_.setScrollBarsShown(true, false);
  viewport_.setScrollBarThickness(8);
  addAndMakeVisible(viewport_);
  viewport_.setViewedComponent(&live_, false);

  neon::ui::styleBtn(save_, true);
  save_.onClick = [this] { doSave(); };
  addAndMakeVisible(save_);
  saveMsg_.setColour(juce::Label::textColourId, neon::ui::muted());
  addAndMakeVisible(saveMsg_);
  banner_.setColour(juce::Label::textColourId, neon::ui::yellow());
  banner_.setFont(juce::Font(juce::FontOptions(13.0f)));
  addAndMakeVisible(banner_);
  stats_.setColour(juce::Label::textColourId, neon::ui::muted());
  stats_.setFont(juce::Font(juce::FontOptions(12.0f)));
  addAndMakeVisible(stats_);

  confirm_.onConfirm = [this] {
    if (confirm_fn_) confirm_fn_();
    confirm_.setVisible(false);
  };
  confirm_.onCancel = [this] { confirm_.setVisible(false); };
  addAndMakeVisible(confirm_);
  confirm_.setVisible(false);
  confirm_.setAlwaysOnTop(true);

  system_.onReboot = [this] {
    showConfirm("Reboot the module?",
                "Clock outputs stop until it comes back up. Unsaved changes are not written.",
                "Reboot", [this] {
                  send([](neon::plugin::DeviceController& c) { c.reboot(); });
                });
  };
  system_.onFactoryReset = [this] {
    showConfirm("Erase every setting?",
                "Every setting and all four presets are erased, including stored networks. "
                "This cannot be undone.",
                "Erase and reboot", [this] {
                  send([](neon::plugin::DeviceController& c) { c.factoryReset(); });
                  have_draft_ = false;
                  dirty_ = false;
                });
  };

  if (auto* c = processor_.controller()) {
    c->setEditorOpen(true);
    const auto b = c->bind_state();
    if (!b.ip.empty())
      hostField_.setText(b.ip);
    else if (!b.connect_host.empty())
      hostField_.setText(b.connect_host);
  }
  if (auto* m = processor_.micController()) {
    m->setEditorOpen(true);
  }

  last_tick_ms_ = juce::Time::getMillisecondCounterHiRes();
  startTimerHz(24);
}

NeonLinkEditor::~NeonLinkEditor() {
  setLookAndFeel(nullptr);
  if (auto* c = processor_.controller()) c->setEditorOpen(false);
  if (auto* m = processor_.micController()) m->setEditorOpen(false);
}

void NeonLinkEditor::send(
    std::function<void(neon::plugin::DeviceController&)> fn) {
  if (auto* c = processor_.controller()) fn(*c);
}

void NeonLinkEditor::sendMic(
    std::function<void(neon::plugin::MicController&)> fn) {
  if (auto* c = processor_.micController()) fn(*c);
}

void NeonLinkEditor::styleTabs() {
  for (int i = 0; i < 7; ++i) neon::ui::styleBtn(*tabs_[i], i == tab_);
}

void NeonLinkEditor::patch(std::function<void(neon::Config&)> fn) {
  if (!have_draft_) return;
  fn(draft_);
  dirty_ = true;
  saveMsg_.setText("Unsaved changes", juce::dontSendNotification);
  saveMsg_.setColour(juce::Label::textColourId, neon::ui::yellow());
  // Reload on the next turn of the message loop. Doing it here re-enters
  // TextEditor::focusLost / a SelectField click and used to delete
  // widgets while JUCE was still on their stack.
  juce::Component::SafePointer<NeonLinkEditor> self(this);
  juce::MessageManager::callAsync([self] {
    if (self == nullptr || !self->have_draft_) return;
    if (self->tab_ == 1)
      self->outputs_.load(self->draft_);
    else if (self->tab_ == 2) {
      if (auto* c = self->processor_.controller()) {
        if (auto snap = c->snapshot()) self->network_.load(self->draft_, *snap);
      }
    } else if (self->tab_ == 3)
      self->midi_.load(self->draft_);
    else if (self->tab_ == 4) {
      if (auto* c = self->processor_.controller()) {
        if (auto snap = c->snapshot()) self->audio_.load(self->draft_, *snap);
      }
    } else if (self->tab_ == 5) {
      if (auto* c = self->processor_.controller()) {
        if (auto snap = c->snapshot()) self->system_.load(self->draft_, *snap);
      }
    }
    self->relayoutPage();
  });
}

void NeonLinkEditor::adoptConfig(const neon::plugin::Snapshot& snap) {
  draft_ = snap.config;
  have_draft_ = true;
  last_config_seq_ = snap.config_seq;
  if (tab_ == 1) outputs_.load(draft_);
  else if (tab_ == 2) network_.load(draft_, snap);
  else if (tab_ == 3) midi_.load(draft_);
  else if (tab_ == 4) audio_.load(draft_, snap);
  else if (tab_ == 5) system_.load(draft_, snap);
  relayoutPage();
}

void NeonLinkEditor::doSave() {
  if (!have_draft_ || !dirty_) return;
  send([this](neon::plugin::DeviceController& c) { c.saveConfig(draft_); });
}

void NeonLinkEditor::showTab(int index) {
  const int prev = tab_;
  tab_ = index;
  styleTabs();
  juce::Component* page = &live_;
  if (index == 1) page = &outputs_;
  else if (index == 2) page = &network_;
  else if (index == 3) page = &midi_;
  else if (index == 4) page = &audio_;
  else if (index == 5) page = &system_;
  else if (index == 6) page = &mic_;
  viewport_.setViewedComponent(page, false);
  if (have_draft_) {
    if (auto* c = processor_.controller()) {
      if (auto snap = c->snapshot()) {
        if (index == 1) outputs_.load(draft_);
        else if (index == 2) network_.load(draft_, *snap);
        else if (index == 3) midi_.load(draft_);
        else if (index == 4) audio_.load(draft_, *snap);
        else if (index == 5) system_.load(draft_, *snap);
      }
    }
  }
  if (index == 4) {
    send([](neon::plugin::DeviceController& c) { c.refreshAudioChannels(); });
  }
  if (index == 6) {
    processor_.ensureControllerStarted();
    sendMic([](neon::plugin::MicController& c) { c.refreshSources(); });
    if (auto* m = processor_.micController()) {
      if (auto snap = m->snapshot()) mic_.load(*snap);
    }
  }
  if (prev == 6 || index == 6) {
    resized();
  } else {
    relayoutPage();
  }
}

void NeonLinkEditor::relayoutPage() {
  auto* page = viewport_.getViewedComponent();
  if (page == nullptr) return;
  int h = 400;
  if (page == &live_) h = live_.preferredHeight();
  else if (page == &outputs_) h = outputs_.preferredHeight();
  else if (page == &network_) h = network_.preferredHeight();
  else if (page == &midi_) h = midi_.preferredHeight();
  else if (page == &audio_) h = audio_.preferredHeight();
  else if (page == &system_) h = system_.preferredHeight();
  else if (page == &mic_) h = mic_.preferredHeight();
  const int w = viewport_.getMaximumVisibleWidth();
  page->setSize(juce::jmax(1, w), h);
}

void NeonLinkEditor::showConfirm(const juce::String& title, const juce::String& body,
                                const juce::String& ok, std::function<void()> fn) {
  confirm_fn_ = std::move(fn);
  confirm_.set(title, body, ok);
  confirm_.setVisible(true);
  confirm_.setBounds(getLocalBounds());
  confirm_.toFront(true);
}

void NeonLinkEditor::paint(juce::Graphics& g) {
  g.fillAll(neon::ui::bg());
  auto r = getLocalBounds().reduced(16);
  r.removeFromTop(28 + 10 + 32 + 10);
  auto tabRow = r.removeFromTop(40);
  g.setColour(neon::ui::surface());
  g.fillRect(tabRow);
  g.setColour(neon::ui::border());
  g.drawRect(tabRow, 1);
}

void NeonLinkEditor::resized() {
  auto r = getLocalBounds().reduced(16);
  auto top = r.removeFromTop(28);
  brand_.setBounds(top.removeFromLeft(130));
  vstVer_.setBounds(top.removeFromLeft(72));
  reach_.setBounds(top.removeFromRight(110).reduced(2, 2));
  chipNet_.setBounds(top.removeFromRight(72).reduced(2, 2));
  chipTransport_.setBounds(top.removeFromRight(56).reduced(2, 2));
  chipSource_.setBounds(top.removeFromRight(56).reduced(2, 2));

  r.removeFromTop(10);
  auto bindRow = r.removeFromTop(32);
  bind_.setBounds(bindRow.removeFromRight(72));
  bindRow.removeFromRight(8);
  hostField_.setBounds(bindRow);

  r.removeFromTop(10);
  auto tabRow = r.removeFromTop(40);
  const int gap = 4;
  const int tw = (tabRow.getWidth() - gap * 6) / 7;
  for (int i = 0; i < 7; ++i) {
    tabs_[i]->setBounds(tabRow.removeFromLeft(tw));
    if (i < 6) tabRow.removeFromLeft(gap);
  }

  r.removeFromTop(8);

  stats_.setBounds(r.removeFromBottom(20));
  banner_.setBounds(r.removeFromBottom(22));
  const bool showSave = tab_ != 6 && (dirty_ || save_.isVisible());
  auto saveRow = r.removeFromBottom(showSave ? 36 : 0);
  if (showSave) {
    r.removeFromBottom(8);
    save_.setBounds(saveRow.removeFromLeft(96));
    saveRow.removeFromLeft(10);
    saveMsg_.setBounds(saveRow);
  }

  viewport_.setBounds(r);
  relayoutPage();
  if (confirm_.isVisible()) confirm_.setBounds(getLocalBounds());
}

void NeonLinkEditor::timerCallback() {
  // The Neon Sync peer lives on the processor, not behind the REST bind:
  // refresh it even while the module snapshot is offline or absent.
  live_.loadNeonSync(processor_.sync().status(), processor_.syncEnabled());
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
    neon::ui::styleChip(reach_,
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
    chipSource_.setText(st.follow_source == "audio" ? "AUDIO"
                             : st.ext_clock         ? "EXT"
                                                    : "LINK",
                        juce::dontSendNotification);
    neon::ui::styleChip(chipSource_, neon::ui::neon(), neon::ui::surface());
    chipTransport_.setText(st.playing ? "RUN" : "STOP", juce::dontSendNotification);
    neon::ui::styleChip(chipTransport_,
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
    neon::ui::styleChip(chipNet_, neon::ui::success(), neon::ui::surface());
  }

  live_.load(*snap, online);

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
  live_.setPhase(ph, static_cast<int>(q), online && st.playing);

  if (snap->has_config &&
      (!have_draft_ || (!dirty_ && snap->config_seq != last_config_seq_))) {
    adoptConfig(*snap);
  }

  if (snap->save_seq != last_save_seq_) {
    last_save_seq_ = snap->save_seq;
    if (snap->last_save_ok) {
      dirty_ = false;
      draft_ = snap->config;
      have_draft_ = true;
      last_config_seq_ = snap->config_seq;
      saveMsg_.setText(snap->save_message.empty() ? "Saved." : snap->save_message,
                       juce::dontSendNotification);
      saveMsg_.setColour(juce::Label::textColourId, neon::ui::success());
      save_ok_until_ = now + 4000.0;
      if (tab_ >= 1) adoptConfig(*snap);
    } else if (!snap->save_message.empty()) {
      saveMsg_.setText(snap->save_message, juce::dontSendNotification);
      saveMsg_.setColour(juce::Label::textColourId, neon::ui::danger());
    }
  }

  if (snap->saving) {
    save_.setButtonText("Saving...");
    save_.setEnabled(false);
    save_.setVisible(tab_ != 6);
  } else {
    save_.setButtonText("Save");
    save_.setEnabled(dirty_);
    save_.setVisible(tab_ != 6 && (dirty_ || now < save_ok_until_));
  }
  if (!dirty_ && now >= save_ok_until_ && !snap->saving) {
    if (saveMsg_.getText() == "Saved.") saveMsg_.setText({}, juce::dontSendNotification);
  }

  banner_.setText(snap->banner, juce::dontSendNotification);
  const juce::String vst = "vst " JucePlugin_VersionString;
  if (online) {
    stats_.setText(vst + "   fw " + juce::String(st.firmware) + "   late " +
                       juce::String(static_cast<int>(st.pulse.late_max_us)) + " us   " +
                       snap->bind.connect_host,
                   juce::dontSendNotification);
  } else {
    stats_.setText(vst + "   bind " + juce::String(snap->bind.connect_host),
                   juce::dontSendNotification);
  }

  if (tab_ == 2) network_.tickStatus(*snap);
  if (tab_ == 4) audio_.tickStatus(*snap);
  if (tab_ == 6) {
    if (auto* m = processor_.micController()) {
      if (auto ms = m->snapshot()) mic_.load(*ms);
    }
  }

  const bool saveVis = save_.isVisible();
  if (saveVis != save_row_vis_) {
    save_row_vis_ = saveVis;
    resized();
  }
}
