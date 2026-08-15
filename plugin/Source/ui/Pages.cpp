#include "Pages.h"

#include <cmath>
#include <cstring>

#include "Tokens.h"

namespace neon::plugin {
namespace {

constexpr int kUnity = 200;
int toPct(int v) { return static_cast<int>(std::lround((v * 100.0) / kUnity)); }
int fromPct(int p) { return juce::jmin(255, static_cast<int>(std::lround((p * kUnity) / 100.0))); }

void setCstr(char* dst, size_t cap, const juce::String& s) {
  const auto u8 = s.toRawUTF8();
  std::strncpy(dst, u8, cap - 1);
  dst[cap - 1] = '\0';
}

juce::String formatUptime(int64_t seconds) {
  const auto h = seconds / 3600;
  const auto m = (seconds % 3600) / 60;
  const auto s = seconds % 60;
  if (h > 0) return juce::String(h) + "h " + juce::String(m) + "m";
  if (m > 0) return juce::String(m) + "m " + juce::String(s) + "s";
  return juce::String(s) + "s";
}

const std::vector<neon::ui::Option> kRoles = {
    {1, "Clock"},
    {2, "Gate while playing"},
    {3, "Reset every loop"},
    {4, "Reset at start"},
    {5, "Reset at stop"},
};

int roleId(OutputRole r) {
  switch (r) {
    case OutputRole::kGate:
      return 2;
    case OutputRole::kResetLoop:
      return 3;
    case OutputRole::kResetStart:
      return 4;
    case OutputRole::kResetStop:
      return 5;
    default:
      return 1;
  }
}

OutputRole roleFrom(int id) {
  switch (id) {
    case 2:
      return OutputRole::kGate;
    case 3:
      return OutputRole::kResetLoop;
    case 4:
      return OutputRole::kResetStart;
    case 5:
      return OutputRole::kResetStop;
    default:
      return OutputRole::kClock;
  }
}

const std::vector<neon::ui::Option> kAudioRoles = {
    {1, "Mix"},
    {2, "Metronome"},
    {3, "Clock (audio)"},
    {4, "Reset (audio)"},
    {5, "Run gate (audio)"},
    {6, "Synth"},
    {7, "Link Audio in"},
    {8, "Line in"},
};

int audioId(AudioRole r) { return static_cast<int>(r) + 1; }
AudioRole audioFrom(int id) {
  return static_cast<AudioRole>(juce::jlimit(0, 7, id - 1));
}

const std::vector<neon::ui::Option> kClockSrc = {
    {1, "Auto — CLK IN wins while patched"},
    {2, "Link is the master"},
    {3, "External input is the master"},
};

int srcId(ClockSource s) {
  switch (s) {
    case ClockSource::kLinkMaster:
      return 2;
    case ClockSource::kExternalMaster:
      return 3;
    default:
      return 1;
  }
}

ClockSource srcFrom(int id) {
  if (id == 2) return ClockSource::kLinkMaster;
  if (id == 3) return ClockSource::kExternalMaster;
  return ClockSource::kAuto;
}

const std::vector<neon::ui::Option> kPolicy = {
    {1, "Ignore — Link is the timeline"},
    {2, "Replace the Link clock"},
    {3, "Merge both sources"},
};

int polId(MidiRouteConfig::ClockPolicy p) {
  switch (p) {
    case MidiRouteConfig::ClockPolicy::kReplace:
      return 2;
    case MidiRouteConfig::ClockPolicy::kMerge:
      return 3;
    default:
      return 1;
  }
}

MidiRouteConfig::ClockPolicy polFrom(int id) {
  if (id == 2) return MidiRouteConfig::ClockPolicy::kReplace;
  if (id == 3) return MidiRouteConfig::ClockPolicy::kMerge;
  return MidiRouteConfig::ClockPolicy::kIgnore;
}

juce::String wifiFailHint(uint32_t reason) {
  switch (reason) {
    case 0:
      return {};
    case 15:
      return "Handshake timed out — usually a wrong password.";
    case 201:
      return "Network not found — check the spelling, and that it is 2.4 GHz.";
    case 2:
    case 202:
    case 204:
      return "Authentication failed — check the password and the WPA mode.";
    default:
      return "Disconnected (reason " + juce::String(static_cast<int>(reason)) +
             ").";
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// Live
// ---------------------------------------------------------------------------

LivePage::LivePage(EditorHost& host) : host_(host) {
  addAndMakeVisible(hero_);
  addAndMakeVisible(phase_);
  beat_.setJustificationType(juce::Justification::centred);
  beat_.setColour(juce::Label::textColourId, neon::ui::text());
  beat_.setFont(juce::Font(juce::FontOptions(72.0f, juce::Font::bold)));
  addAndMakeVisible(beat_);
  beat_.setVisible(false);

  auto hook = [this](juce::TextButton& b, auto fn) {
    neon::ui::styleBtn(b, false);
    b.onClick = [this, fn] { host_.send(fn); };
    addAndMakeVisible(b);
  };
  neon::ui::styleBtn(play_, true);
  play_.onClick = [this] {
    host_.send([](DeviceController& c) {
      c.transport(neon::client::TransportOp::Toggle);
    });
  };
  addAndMakeVisible(play_);
  hook(tap_, [](DeviceController& c) { c.tempoOp(neon::client::TempoOp::Tap); });
  hook(minus_, [](DeviceController& c) { c.tempoOp(neon::client::TempoOp::Nudge, -1); });
  hook(plus_, [](DeviceController& c) { c.tempoOp(neon::client::TempoOp::Nudge, 1); });
  hook(half_, [](DeviceController& c) { c.tempoOp(neon::client::TempoOp::Half); });
  hook(double_, [](DeviceController& c) { c.tempoOp(neon::client::TempoOp::Double); });

  bpm_.setInputRestrictions(7, "0123456789.");
  bpm_.setTextToShowWhenEmpty("120.0", neon::ui::muted());
  neon::ui::styleField(bpm_);
  addAndMakeVisible(bpm_);
  neon::ui::styleBtn(setBpm_, false);
  setBpm_.onClick = [this] {
    const double v = bpm_.getText().getDoubleValue();
    if (v >= 20.0 && v <= 999.0) {
      host_.send([v](DeviceController& c) { c.setTempo(v); });
      bpm_.clear();
    }
  };
  addAndMakeVisible(setBpm_);

  extras_.setItems({"Sync", "Set", "Stats"}, 0);
  extras_.onChange = [this](int i) {
    pane_ = i;
    resized();
  };
  addAndMakeVisible(extras_);

  hook(resyncNext_, [](DeviceController& c) { c.resync(neon::client::ResyncOp::Next); });
  hook(resyncNow_, [](DeviceController& c) { c.resync(neon::client::ResyncOp::Now); });

  for (int i = 0; i < 4; ++i) {
    slotLab_[i].setText("Slot " + juce::String(i + 1), juce::dontSendNotification);
    slotLab_[i].setColour(juce::Label::textColourId, neon::ui::muted());
    save_[i].setButtonText("Save");
    recall_[i].setButtonText("Recall");
    neon::ui::styleBtn(save_[i], false);
    neon::ui::styleBtn(recall_[i], false);
    save_[i].onClick = [this, i] {
      host_.send([i](DeviceController& c) {
        c.preset(neon::client::PresetOp::Save, i);
      });
      presetMsg_.setText("Slot " + juce::String(i + 1) + " saved.",
                         juce::dontSendNotification);
    };
    recall_[i].onClick = [this, i] {
      host_.send([i](DeviceController& c) {
        c.preset(neon::client::PresetOp::Recall, i);
      });
      presetMsg_.setText("Slot " + juce::String(i + 1) + " recalled.",
                         juce::dontSendNotification);
    };
    addAndMakeVisible(slotLab_[i]);
    addAndMakeVisible(save_[i]);
    addAndMakeVisible(recall_[i]);
  }
  presetMsg_.setColour(juce::Label::textColourId, neon::ui::muted());
  addAndMakeVisible(presetMsg_);
  addAndMakeVisible(edges_);
  addAndMakeVisible(lateMax_);
  addAndMakeVisible(lateAvg_);
  addAndMakeVisible(uptime_);
}

void LivePage::load(const Snapshot& snap, bool online) {
  const auto& st = snap.status;
  const bool playing = st.playing;
  const bool big = snap.has_config && snap.config.big_beat_display != 0;
  show_beat_ = online && playing && big;
  const uint32_t q = st.quantum != 0 ? st.quantum : 4;
  const int beat = static_cast<int>(st.phase_milli / 1000) % static_cast<int>(q) + 1;
  beat_.setText(juce::String(beat), juce::dontSendNotification);
  beat_.setVisible(show_beat_);
  hero_.setVisible(!show_beat_);
  hero_.setBpm(st.bpm, online && st.tempo_valid);
  play_.setButtonText(playing ? "Stop" : "Play");
  neon::ui::styleBtn(play_, !playing);
  edges_.set("Edges emitted", online ? juce::String(static_cast<int>(st.pulse.edges)) : "—");
  lateMax_.set("Worst lateness",
               online ? juce::String(static_cast<int>(st.pulse.late_max_us)) + " µs" : "—");
  lateAvg_.set("Average lateness",
               online ? juce::String(static_cast<int>(st.pulse.late_avg_us)) + " µs" : "—");
  uptime_.set("Uptime", online ? formatUptime(st.uptime_s) : "—");
}

void LivePage::setPhase(float phase01, int quantum, bool running) {
  phase_.setPhase(phase01, quantum, running);
}

void LivePage::paint(juce::Graphics& g) {
  auto r = getLocalBounds();
  auto card = r.removeFromTop(268);
  g.setColour(neon::ui::surface());
  g.fillRect(card);
  g.setColour(neon::ui::border());
  g.drawRect(card, 1);
}

void LivePage::resized() {
  auto r = getLocalBounds();
  auto card = r.removeFromTop(268).reduced(12);
  if (show_beat_)
    beat_.setBounds(card.removeFromTop(72));
  else
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
  card.removeFromTop(10);
  auto tempo = card.removeFromTop(32);
  setBpm_.setBounds(tempo.removeFromRight(96));
  tempo.removeFromRight(8);
  bpm_.setBounds(tempo);

  r.removeFromTop(12);
  extras_.setBounds(r.removeFromTop(28));
  r.removeFromTop(10);

  const bool sync = pane_ == 0;
  const bool set = pane_ == 1;
  const bool stats = pane_ == 2;
  resyncNext_.setVisible(sync);
  resyncNow_.setVisible(sync);
  for (int i = 0; i < 4; ++i) {
    slotLab_[i].setVisible(set);
    save_[i].setVisible(set);
    recall_[i].setVisible(set);
  }
  presetMsg_.setVisible(set);
  edges_.setVisible(stats);
  lateMax_.setVisible(stats);
  lateAvg_.setVisible(stats);
  uptime_.setVisible(stats);

  if (sync) {
    auto s = r.removeFromTop(32);
    resyncNext_.setBounds(s.removeFromLeft(s.getWidth() / 2 - 4));
    s.removeFromLeft(8);
    resyncNow_.setBounds(s);
  } else if (set) {
    for (int i = 0; i < 4; ++i) {
      auto pr = r.removeFromTop(28);
      slotLab_[i].setBounds(pr.removeFromLeft(56));
      save_[i].setBounds(pr.removeFromLeft(pr.getWidth() / 2 - 4));
      pr.removeFromLeft(8);
      recall_[i].setBounds(pr);
      r.removeFromTop(6);
    }
    presetMsg_.setBounds(r.removeFromTop(20));
  } else {
    edges_.setBounds(r.removeFromTop(22));
    r.removeFromTop(4);
    lateMax_.setBounds(r.removeFromTop(22));
    r.removeFromTop(4);
    lateAvg_.setBounds(r.removeFromTop(22));
    r.removeFromTop(4);
    uptime_.setBounds(r.removeFromTop(22));
  }
}

// ---------------------------------------------------------------------------
// Outputs
// ---------------------------------------------------------------------------

juce::String OutputsPage::jackTitle(Jack j) {
  switch (j) {
    case SpdifIn:
      return "SPDIF IN";
    case SpdifOut:
      return "SPDIF OUT";
    case LineIn:
      return "LINE IN";
    case LineOut:
      return "LINE OUT";
    case MidiIn:
      return "MIDI IN";
    case MidiOut:
      return "MIDI OUT";
    case Cv1In:
      return "CV IN 1";
    case Cv1Out:
      return "CV OUT 1";
    case Cv2In:
      return "CV IN 2";
    case Cv2Out:
      return "CV OUT 2";
  }
  return {};
}

juce::String OutputsPage::jackTag(Jack j, const neon::Config& cfg) {
  switch (j) {
    case Cv1Out:
      return cfg.midi.pitch_cv ? "PITCH" : "TEMPO";
    case Cv2Out: {
      const auto& c = cfg.engine.clocks[0];
      if (!c.enabled) return "OFF";
      if (c.role == OutputRole::kGate) return "GATE";
      if (c.role != OutputRole::kClock) return "RST";
      return "CLK";
    }
    case Cv1In:
      return "CLK IN";
    case Cv2In:
      return "RST IN";
    case MidiOut:
      return cfg.midi_clock_out ? "MCLK" : "MIDI";
    case MidiIn:
      return "IN";
    case LineOut:
      if (!cfg.audio.enabled) return "OFF";
      return cfg.audio.role_l == cfg.audio.role_r ? "MIX" : "L·R";
    case LineIn:
      return cfg.audio.enabled ? "LINE" : "OFF";
    default:
      return "SOON";
  }
}

OutputsPage::OutputsPage(EditorHost& host) : host_(host) {
  static const char* kHoleLab[] = {"SPDIF IN", "SPDIF OUT", "LINE IN", "LINE OUT",
                                   "MIDI IN",  "MIDI OUT",  "CV1 IN",  "CV1 OUT",
                                   "CV2 IN",   "CV2 OUT"};
  for (int i = 0; i < 10; ++i) {
    auto* b = holes_.add(new juce::TextButton(kHoleLab[i]));
    neon::ui::styleBtn(*b, i == static_cast<int>(jack_));
    b->onClick = [this, i] { selectJack(static_cast<Jack>(i)); };
    addAndMakeVisible(b);
  }
  jackHead_.setColour(juce::Label::textColourId, neon::ui::text());
  jackHead_.setFont(juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));
  jackCopy_.setColour(juce::Label::textColourId, neon::ui::muted());
  jackCopy_.setFont(juce::Font(juce::FontOptions(12.0f)));
  addAndMakeVisible(jackHead_);
  addAndMakeVisible(jackCopy_);

  auto add = [this](juce::Component& c) { addAndMakeVisible(c); };
  add(enabled_);
  add(role_);
  add(clockNav_);
  add(ppqn_);
  add(mult_);
  add(div_);
  add(mode_);
  add(trig_);
  add(duty_);
  add(freeRun_);
  add(shuffle_);
  add(rhythm_);
  add(steps_);
  add(fills_);
  add(rotate_);
  add(chance_);
  add(jitter_);
  add(overLoop_);
  add(grid_);
  add(cv1Role_);
  add(cvMin_);
  add(cvMax_);
  add(clkSrc_);
  add(clkIn_);
  add(midiClkOut_);
  add(midiNudge_);
  add(midiPolicy_);
  add(midiTransport_);
  add(audioEn_);
  add(roleL_);
  add(roleR_);
  add(lineMon_);

  virtHead_.setText("Virtual CLK 2–4", juce::dontSendNotification);
  virtHead_.setColour(juce::Label::textColourId, neon::ui::text());
  virtHead_.setFont(juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));
  add(virtHead_);
  virtNav_.setItems({"2", "3", "4"}, 0);
  virtNav_.onChange = [this](int i) {
    virt_ = i + 1;
    editing_virt_ = true;
    bindClock(virt_, true);
    resized();
  };
  add(virtNav_);
}

void OutputsPage::selectJack(Jack j) {
  jack_ = j;
  editing_virt_ = j != Cv2Out;
  for (int i = 0; i < holes_.size(); ++i)
    neon::ui::styleBtn(*holes_[i], i == static_cast<int>(jack_));
  if (j == Cv2Out)
    bindClock(0, false);
  else
    bindClock(virt_, true);
  resized();
}

void OutputsPage::bindClock(int index, bool /*virt*/) {
  auto& c = cfg_.engine.clocks[index];
  enabled_.setLabel("Enabled");
  enabled_.setValue(c.enabled);
  enabled_.onChange = [this, index](bool v) {
    host_.patch([index, v](neon::Config& d) { d.engine.clocks[index].enabled = v; });
  };
  role_.set(index == 0 ? "This jack" : "Role", roleId(c.role), kRoles);
  role_.onChange = [this, index](int id) {
    host_.patch([index, id](neon::Config& d) { d.engine.clocks[index].role = roleFrom(id); });
  };
  clockNav_.setItems({"Shape", "Groove"}, clock_pane_);
  clockNav_.onChange = [this](int i) {
    clock_pane_ = i;
    resized();
  };
  ppqn_.set("PPQN", static_cast<int>(c.ppqn), 1, 192, 1);
  ppqn_.onChange = [this, index](int v) {
    host_.patch([index, v](neon::Config& d) { d.engine.clocks[index].ppqn = static_cast<uint32_t>(v); });
  };
  mult_.set("×", static_cast<int>(c.mult), 1, 16);
  mult_.onChange = [this, index](int v) {
    host_.patch([index, v](neon::Config& d) { d.engine.clocks[index].mult = static_cast<uint32_t>(v); });
  };
  div_.set("÷", static_cast<int>(c.div), 1, 16);
  div_.onChange = [this, index](int v) {
    host_.patch([index, v](neon::Config& d) { d.engine.clocks[index].div = static_cast<uint32_t>(v); });
  };
  mode_.set("Mode", c.mode == ClockOutputConfig::PulseMode::kSquare ? 2 : 1,
            {{1, "Trigger"}, {2, "Square"}});
  mode_.onChange = [this, index](int id) {
    host_.patch([index, id](neon::Config& d) {
      d.engine.clocks[index].mode = id == 2 ? ClockOutputConfig::PulseMode::kSquare
                                           : ClockOutputConfig::PulseMode::kTrigger;
    });
  };
  trig_.set("Trig µs", static_cast<int>(c.trig_len_us), 1000, 100000, 1000);
  trig_.onChange = [this, index](int v) {
    host_.patch([index, v](neon::Config& d) {
      d.engine.clocks[index].trig_len_us = static_cast<uint32_t>(v);
    });
  };
  duty_.set("Duty %", c.duty_pct, 1, 99);
  duty_.onChange = [this, index](int v) {
    host_.patch([index, v](neon::Config& d) {
      d.engine.clocks[index].duty_pct = static_cast<uint8_t>(v);
    });
  };
  freeRun_.setLabel("Free run (ignore transport stop)");
  freeRun_.setValue(c.free_run);
  freeRun_.onChange = [this, index](bool v) {
    host_.patch([index, v](neon::Config& d) { d.engine.clocks[index].free_run = v; });
  };
  shuffle_.set("Shuffle %", c.shuffle_pct, 0, 75);
  shuffle_.onChange = [this, index](int v) {
    host_.patch([index, v](neon::Config& d) {
      d.engine.clocks[index].shuffle_pct = static_cast<uint8_t>(v);
    });
  };
  const int rid = c.rhythm == ClockOutputConfig::RhythmMode::kEuclid       ? 2
                  : c.rhythm == ClockOutputConfig::RhythmMode::kProbability ? 3
                  : c.rhythm == ClockOutputConfig::RhythmMode::kPattern     ? 4
                                                                            : 1;
  rhythm_.set("Pattern", rid,
              {{1, "Every pulse"},
               {2, "Euclidean"},
               {3, "Chance"},
               {4, "Free steps"}});
  rhythm_.onChange = [this, index](int id) {
    host_.patch([index, id](neon::Config& d) {
      auto& r = d.engine.clocks[index].rhythm;
      if (id == 2)
        r = ClockOutputConfig::RhythmMode::kEuclid;
      else if (id == 3)
        r = ClockOutputConfig::RhythmMode::kProbability;
      else if (id == 4)
        r = ClockOutputConfig::RhythmMode::kPattern;
      else
        r = ClockOutputConfig::RhythmMode::kAll;
    });
  };
  steps_.set("Steps", c.euclid_steps, 1, 64);
  steps_.onChange = [this, index](int v) {
    host_.patch([index, v](neon::Config& d) {
      d.engine.clocks[index].euclid_steps = static_cast<uint8_t>(v);
    });
  };
  fills_.set("Fills", c.euclid_fills, 0, 64);
  fills_.onChange = [this, index](int v) {
    host_.patch([index, v](neon::Config& d) {
      d.engine.clocks[index].euclid_fills = static_cast<uint8_t>(v);
    });
  };
  rotate_.set("Rotate", c.euclid_rot, 0, 63);
  rotate_.onChange = [this, index](int v) {
    host_.patch([index, v](neon::Config& d) {
      d.engine.clocks[index].euclid_rot = static_cast<uint8_t>(v);
    });
  };
  chance_.set("Chance %", c.probability_pct, 0, 100);
  chance_.onChange = [this, index](int v) {
    host_.patch([index, v](neon::Config& d) {
      d.engine.clocks[index].probability_pct = static_cast<uint8_t>(v);
    });
  };
  jitter_.set("Jitter %", c.humanize_pct, 0, 50);
  jitter_.onChange = [this, index](int v) {
    host_.patch([index, v](neon::Config& d) {
      d.engine.clocks[index].humanize_pct = static_cast<uint8_t>(v);
    });
  };
  overLoop_.setLabel("Steps span the loop");
  overLoop_.setValue(c.rhythm_over_loop);
  overLoop_.onChange = [this, index](bool v) {
    host_.patch([index, v](neon::Config& d) { d.engine.clocks[index].rhythm_over_loop = v; });
  };
  grid_.set(c.euclid_steps, c.step_mask);
  grid_.onToggle = [this, index](int step) {
    host_.patch([index, step](neon::Config& d) {
      d.engine.clocks[index].step_mask ^= (1ull << step);
    });
  };
}

void OutputsPage::load(const neon::Config& cfg) {
  cfg_ = cfg;
  for (int i = 0; i < holes_.size(); ++i) {
    const auto tag = jackTag(static_cast<Jack>(i), cfg);
    holes_[i]->setButtonText(jackTitle(static_cast<Jack>(i)) + "  " + tag);
  }
  jackHead_.setText(jackTitle(jack_), juce::dontSendNotification);

  cv1Role_.set("This jack", cfg.midi.pitch_cv ? 2 : 1,
               {{1, "Tempo CV (0–5 V)"}, {2, "Pitch CV (1 V/oct)"}});
  cv1Role_.onChange = [this](int id) {
    host_.patch([id](neon::Config& d) { d.midi.pitch_cv = id == 2; });
  };
  cvMin_.set("Minimum", cfg.tempo_cv_min_bpm, 1, 998, 1, "BPM at 0 V");
  cvMin_.onChange = [this](int v) {
    host_.patch([v](neon::Config& d) { d.tempo_cv_min_bpm = static_cast<uint16_t>(v); });
  };
  cvMax_.set("Maximum", cfg.tempo_cv_max_bpm, 2, 999, 1, "BPM at 5 V");
  cvMax_.onChange = [this](int v) {
    host_.patch([v](neon::Config& d) { d.tempo_cv_max_bpm = static_cast<uint16_t>(v); });
  };
  clkSrc_.set("Source", srcId(cfg.clock_source), kClockSrc);
  clkSrc_.onChange = [this](int id) {
    host_.patch([id](neon::Config& d) { d.clock_source = srcFrom(id); });
  };
  clkIn_.set("CLK IN rate", static_cast<int>(cfg.clock_in_ppqn), 1, 96, 1,
             "Pulses per quarter note");
  clkIn_.onChange = [this](int v) {
    host_.patch([v](neon::Config& d) { d.clock_in_ppqn = static_cast<uint32_t>(v); });
  };
  midiClkOut_.setLabel("Send MIDI clock on this jack");
  midiClkOut_.setValue(cfg.midi_clock_out != 0);
  midiClkOut_.onChange = [this](bool v) {
    host_.patch([v](neon::Config& d) { d.midi_clock_out = v ? 1 : 0; });
  };
  midiNudge_.set("MIDI nudge", cfg.midi_nudge_us, -100000, 100000, 500,
                 "µs — MIDI only, independent of CV latency");
  midiNudge_.onChange = [this](int v) {
    host_.patch([v](neon::Config& d) { d.midi_nudge_us = v; });
  };
  midiPolicy_.set("Incoming clock", polId(cfg.midi.clock_policy), kPolicy);
  midiPolicy_.onChange = [this](int id) {
    host_.patch([id](neon::Config& d) { d.midi.clock_policy = polFrom(id); });
  };
  midiTransport_.setLabel("Follow transport messages");
  midiTransport_.setValue(cfg.midi.transport_enabled);
  midiTransport_.onChange = [this](bool v) {
    host_.patch([v](neon::Config& d) { d.midi.transport_enabled = v; });
  };
  audioEn_.setLabel("Audio engine enabled (takes effect on reboot)");
  audioEn_.setValue(cfg.audio.enabled != 0);
  audioEn_.onChange = [this](bool v) {
    host_.patch([v](neon::Config& d) { d.audio.enabled = v ? 1 : 0; });
  };
  roleL_.set("Left carries", audioId(cfg.audio.role_l), kAudioRoles);
  roleL_.onChange = [this](int id) {
    host_.patch([id](neon::Config& d) { d.audio.role_l = audioFrom(id); });
  };
  roleR_.set("Right carries", audioId(cfg.audio.role_r), kAudioRoles);
  roleR_.onChange = [this](int id) {
    host_.patch([id](neon::Config& d) { d.audio.role_r = audioFrom(id); });
  };
  lineMon_.set("Monitor %", toPct(cfg.audio.linein_monitor_gain), 0, 127);
  lineMon_.onChange = [this](int v) {
    host_.patch([v](neon::Config& d) {
      d.audio.linein_monitor_gain = static_cast<uint8_t>(fromPct(v));
    });
  };

  virtNav_.setItems({cfg.engine.clocks[1].enabled ? "2" : "2·",
                     cfg.engine.clocks[2].enabled ? "3" : "3·",
                     cfg.engine.clocks[3].enabled ? "4" : "4·"},
                    virt_ - 1);

  if (jack_ == Cv2Out)
    bindClock(0, false);
  else
    bindClock(virt_, true);
  resized();
}

int OutputsPage::preferredHeight() const {
  return 980;
}

void OutputsPage::paint(juce::Graphics&) {}

void OutputsPage::resized() {
  neon::ui::Stack st{getLocalBounds()};
  auto holes = st.next(130);
  const int col = holes.getWidth() / 2;
  for (int i = 0; i < 10; ++i) {
    const int row = i / 2;
    const int c = i % 2;
    holes_[i]->setBounds(holes.getX() + c * col + 2, holes.getY() + row * 26, col - 4,
                         24);
  }

  jackHead_.setBounds(st.next(20));
  jackCopy_.setBounds(st.next(36));

  const bool clkJack = jack_ == Cv2Out;

  auto hideClock = [this, clkJack](bool show) {
    enabled_.setVisible(show);
    role_.setVisible(show);
    const bool isClk = show && cfg_.engine.clocks[clkJack ? 0 : virt_].role ==
                                   OutputRole::kClock;
    clockNav_.setVisible(isClk);
    const bool shape = isClk && clock_pane_ == 0;
    const bool groove = isClk && clock_pane_ == 1;
    const auto& c = cfg_.engine.clocks[clkJack ? 0 : virt_];
    ppqn_.setVisible(shape);
    mult_.setVisible(shape);
    div_.setVisible(shape);
    mode_.setVisible(shape);
    trig_.setVisible(shape && c.mode == ClockOutputConfig::PulseMode::kTrigger);
    duty_.setVisible(shape && c.mode == ClockOutputConfig::PulseMode::kSquare);
    freeRun_.setVisible(shape);
    shuffle_.setVisible(groove);
    rhythm_.setVisible(groove);
    steps_.setVisible(groove && (c.rhythm == ClockOutputConfig::RhythmMode::kEuclid ||
                                 c.rhythm == ClockOutputConfig::RhythmMode::kPattern));
    fills_.setVisible(groove && c.rhythm == ClockOutputConfig::RhythmMode::kEuclid);
    rotate_.setVisible(groove && c.rhythm == ClockOutputConfig::RhythmMode::kEuclid);
    chance_.setVisible(groove && c.rhythm != ClockOutputConfig::RhythmMode::kAll);
    jitter_.setVisible(groove);
    overLoop_.setVisible(groove && c.rhythm != ClockOutputConfig::RhythmMode::kAll);
    grid_.setVisible(groove && c.rhythm == ClockOutputConfig::RhythmMode::kPattern);
  };

  auto placeClock = [&]() {
    if (enabled_.isVisible()) enabled_.setBounds(st.next(28));
    if (role_.isVisible()) role_.setBounds(st.next(48));
    if (clockNav_.isVisible()) clockNav_.setBounds(st.next(28));
    if (ppqn_.isVisible()) ppqn_.setBounds(st.next(48));
    if (mult_.isVisible()) mult_.setBounds(st.next(48));
    if (div_.isVisible()) div_.setBounds(st.next(48));
    if (mode_.isVisible()) mode_.setBounds(st.next(48));
    if (trig_.isVisible()) trig_.setBounds(st.next(48));
    if (duty_.isVisible()) duty_.setBounds(st.next(48));
    if (freeRun_.isVisible()) freeRun_.setBounds(st.next(28));
    if (shuffle_.isVisible()) shuffle_.setBounds(st.next(48));
    if (rhythm_.isVisible()) rhythm_.setBounds(st.next(48));
    if (steps_.isVisible()) steps_.setBounds(st.next(48));
    if (fills_.isVisible()) fills_.setBounds(st.next(48));
    if (rotate_.isVisible()) rotate_.setBounds(st.next(48));
    if (chance_.isVisible()) chance_.setBounds(st.next(48));
    if (jitter_.isVisible()) jitter_.setBounds(st.next(48));
    if (grid_.isVisible()) grid_.setBounds(st.next(80));
    if (overLoop_.isVisible()) overLoop_.setBounds(st.next(28));
  };

  cv1Role_.setVisible(jack_ == Cv1Out);
  cvMin_.setVisible(jack_ == Cv1Out && !cfg_.midi.pitch_cv);
  cvMax_.setVisible(jack_ == Cv1Out && !cfg_.midi.pitch_cv);
  clkSrc_.setVisible(jack_ == Cv1In);
  clkIn_.setVisible(jack_ == Cv1In);
  midiClkOut_.setVisible(jack_ == MidiOut);
  midiNudge_.setVisible(jack_ == MidiOut);
  midiPolicy_.setVisible(jack_ == MidiIn);
  midiTransport_.setVisible(jack_ == MidiIn);
  audioEn_.setVisible(jack_ == LineOut);
  roleL_.setVisible(jack_ == LineOut);
  roleR_.setVisible(jack_ == LineOut);
  lineMon_.setVisible(jack_ == LineIn);

  switch (jack_) {
    case Cv2Out:
      jackCopy_.setText("Physical clock / gate. This is CLK 1, wired to CV out 2.",
                        juce::dontSendNotification);
      if (!editing_virt_) {
        hideClock(true);
        placeClock();
      } else {
        hideClock(false);
      }
      break;
    case Cv1Out:
      hideClock(false);
      jackCopy_.setText("Tempo CV on CV out 1. Pitch mode steals the jack for 1 V/oct.",
                        juce::dontSendNotification);
      cv1Role_.setBounds(st.next(48));
      if (cvMin_.isVisible()) cvMin_.setBounds(st.next(62));
      if (cvMax_.isVisible()) cvMax_.setBounds(st.next(62));
      break;
    case Cv1In:
      hideClock(false);
      jackCopy_.setText("External clock in. Rising edges ≥ 1 V set the Link tempo.",
                        juce::dontSendNotification);
      clkSrc_.setBounds(st.next(48));
      clkIn_.setBounds(st.next(62));
      break;
    case Cv2In:
      hideClock(false);
      jackCopy_.setText("Reset in. A rising edge lines the downbeat up with the pulse.",
                        juce::dontSendNotification);
      break;
    case MidiOut:
      hideClock(false);
      jackCopy_.setText("TRS MIDI out (Type A). Notes always leave here.",
                        juce::dontSendNotification);
      midiClkOut_.setBounds(st.next(28));
      midiNudge_.setBounds(st.next(62));
      break;
    case MidiIn:
      hideClock(false);
      jackCopy_.setText("TRS MIDI in. Clock and transport from a hardware box land here.",
                        juce::dontSendNotification);
      midiPolicy_.setBounds(st.next(48));
      midiTransport_.setBounds(st.next(28));
      break;
    case LineOut:
      hideClock(false);
      jackCopy_.setText("Stereo line out from the audio engine.",
                        juce::dontSendNotification);
      audioEn_.setBounds(st.next(28));
      roleL_.setBounds(st.next(48));
      roleR_.setBounds(st.next(48));
      break;
    case LineIn:
      hideClock(false);
      jackCopy_.setText("Stereo line in. Feed it into the mix here.",
                        juce::dontSendNotification);
      lineMon_.setBounds(st.next(48));
      break;
    default:
      hideClock(false);
      jackCopy_.setText("S/PDIF is on the AMYboard but not driven in this firmware.",
                        juce::dontSendNotification);
      break;
  }

  virtHead_.setBounds(st.next(22));
  virtNav_.setBounds(st.next(28));
  if (jack_ != Cv2Out || editing_virt_) {
    hideClock(true);
    placeClock();
  }
}

// ---------------------------------------------------------------------------
// Network
// ---------------------------------------------------------------------------

NetworkPage::NetworkPage(EditorHost& host) : host_(host) {
  nav_.setItems({"Now", "Join", "AP"}, 0);
  nav_.onChange = [this](int i) {
    pane_ = i;
    resized();
  };
  addAndMakeVisible(nav_);
  neon::ui::styleChip(netChip_, neon::ui::success(), neon::ui::surface());
  addAndMakeVisible(netChip_);
  addAndMakeVisible(addr_);
  addAndMakeVisible(hostname_);
  addAndMakeVisible(trying_);
  addAndMakeVisible(ap_);
  addAndMakeVisible(peers_);

  slots_.onChange = [this](int i) {
    slot_ = i;
    load(cfg_, last_snap_);
    resized();
  };
  addAndMakeVisible(slots_);
  addAndMakeVisible(ssid_);
  addAndMakeVisible(pass_);
  addAndMakeVisible(retries_);
  addAndMakeVisible(hidden_);
  neon::ui::styleBtn(scan_, false);
  scan_.onClick = [this] {
    host_.send([](DeviceController& c) { c.scanWifi(); });
  };
  addAndMakeVisible(scan_);
  scanMsg_.setColour(juce::Label::textColourId, neon::ui::muted());
  addAndMakeVisible(scanMsg_);

  addAndMakeVisible(policy_);
  addAndMakeVisible(apSsid_);
  addAndMakeVisible(apPass_);
  addAndMakeVisible(channel_);
  addAndMakeVisible(requirePass_);
  addAndMakeVisible(apHidden_);
}

void NetworkPage::load(const neon::Config& cfg, const Snapshot& snap) {
  cfg_ = cfg;
  last_snap_ = snap;
  if (snap.has_config) secrets_ = snap.secrets;

  const auto& st = snap.status;
  juce::String net = "OFF";
  if (st.setup_ap)
    net = "AP";
  else if (st.network == neon::client::NetworkKind::Ethernet)
    net = "ETH";
  else if (st.network == neon::client::NetworkKind::Wifi)
    net = "STA";
  netChip_.setText(net, juce::dontSendNotification);
  addr_.set("Address", st.ip.empty() ? "—" : st.ip);
  hostname_.set("Hostname", st.hostname.empty() ? "—" : st.hostname);
  trying_.set("Trying", st.wifi_ssid.empty() ? "none" : st.wifi_ssid);
  ap_.set("Access point", st.ap_ssid.empty() ? "—" : st.ap_ssid);
  peers_.set("Link peers", juce::String(static_cast<int>(st.peers)));
  const auto hint = wifiFailHint(st.wifi_fail_reason);

  std::vector<juce::String> labs;
  for (int i = 0; i < neon::kWifiSlots; ++i) {
    const juce::String ss = juce::String::fromUTF8(cfg.wifi[i].ssid);
    labs.push_back(ss.isEmpty() ? juce::String(i + 1) : juce::String(i + 1) + " " + ss.substring(0, 8));
  }
  slots_.setItems(labs, slot_);

  const auto& n = cfg.wifi[slot_];
  ssid_.set("Network name", juce::String::fromUTF8(n.ssid), 32, "2.4 GHz only");
  ssid_.onChange = [this](juce::String v) {
    host_.patch([this, v](neon::Config& d) {
      if (std::strcmp(d.wifi[slot_].ssid, v.toRawUTF8()) != 0) d.wifi[slot_].pass[0] = '\0';
      setCstr(d.wifi[slot_].ssid, sizeof(d.wifi[slot_].ssid), v);
    });
  };
  const juce::String ph = secrets_.wifi_has_pass[slot_] ? "•••••••• (unchanged)" : "None";
  pass_.set("Password", juce::String::fromUTF8(n.pass), 64, {}, true, ph);
  pass_.onChange = [this](juce::String v) {
    host_.patch([this, v](neon::Config& d) {
      setCstr(d.wifi[slot_].pass, sizeof(d.wifi[slot_].pass), v);
    });
  };
  retries_.set("Attempts", cfg.wifi_retries, 1, 10);
  retries_.onChange = [this](int v) {
    host_.patch([v](neon::Config& d) { d.wifi_retries = static_cast<uint8_t>(v); });
  };
  hidden_.setLabel("Hidden network");
  hidden_.setValue(n.hidden != 0);
  hidden_.onChange = [this](bool v) {
    host_.patch([this, v](neon::Config& d) { d.wifi[slot_].hidden = v ? 1 : 0; });
  };
  scan_.setButtonText(snap.scanning ? "Scanning…" : "Scan");
  scan_.setEnabled(!snap.scanning);
  juce::String sm = snap.scan_message;
  if (hint.isNotEmpty() && pane_ == 0) sm = hint;
  scanMsg_.setText(sm, juce::dontSendNotification);

  found_.clear();
  for (const auto& s : snap.scan) {
    auto* b = found_.add(new juce::TextButton(
        juce::String(s.ssid) + " · " + juce::String(s.rssi) + "dBm" +
        (s.open ? " · open" : "")));
    neon::ui::styleBtn(*b, false);
    const std::string ssid = s.ssid;
    b->onClick = [this, ssid] {
      host_.patch([ssid](neon::Config& d) {
        int slot = -1;
        for (int i = 0; i < neon::kWifiSlots; ++i) {
          if (d.wifi[i].ssid[0] == '\0') {
            slot = i;
            break;
          }
        }
        if (slot < 0) return;
        setCstr(d.wifi[slot].ssid, sizeof(d.wifi[slot].ssid), juce::String(ssid));
        d.wifi[slot].pass[0] = '\0';
      });
    };
    addAndMakeVisible(b);
  }

  policy_.set("Create",
              cfg.ap_policy == ApPolicy::kAlways ? 2 : cfg.ap_policy == ApPolicy::kOff ? 3 : 1,
              {{1, "When no network is reachable"},
               {2, "Always — never join a network"},
               {3, "Never"}});
  policy_.onChange = [this](int id) {
    host_.patch([id](neon::Config& d) {
      d.ap_policy = id == 2 ? ApPolicy::kAlways : id == 3 ? ApPolicy::kOff : ApPolicy::kFallback;
    });
  };
  apSsid_.set("Network name", juce::String::fromUTF8(cfg.ap_ssid), 32,
              "Blank derives it from the device name", false,
              juce::String::fromUTF8(cfg.device_name).toUpperCase() + "-XXXX");
  apSsid_.onChange = [this](juce::String v) {
    host_.patch([v](neon::Config& d) { setCstr(d.ap_ssid, sizeof(d.ap_ssid), v); });
  };
  apPass_.set("Password", juce::String::fromUTF8(cfg.ap_pass), 64, {}, true,
              secrets_.ap_has_pass ? "•••••••• (unchanged)" : "None");
  apPass_.onChange = [this](juce::String v) {
    host_.patch([v](neon::Config& d) { setCstr(d.ap_pass, sizeof(d.ap_pass), v); });
  };
  channel_.set("Channel", cfg.ap_channel, 1, 13);
  channel_.onChange = [this](int v) {
    host_.patch([v](neon::Config& d) { d.ap_channel = static_cast<uint8_t>(v); });
  };
  requirePass_.setLabel("Require a password");
  requirePass_.setValue(cfg.ap_require_pass != 0);
  requirePass_.onChange = [this](bool v) {
    host_.patch([v](neon::Config& d) { d.ap_require_pass = v ? 1 : 0; });
  };
  apHidden_.setLabel("Hidden network");
  apHidden_.setValue(cfg.ap_hidden != 0);
  apHidden_.onChange = [this](bool v) {
    host_.patch([v](neon::Config& d) { d.ap_hidden = v ? 1 : 0; });
  };
  resized();
}

void NetworkPage::tickStatus(const Snapshot& snap) {
  const auto& st = snap.status;
  juce::String net = "OFF";
  if (st.setup_ap)
    net = "AP";
  else if (st.network == neon::client::NetworkKind::Ethernet)
    net = "ETH";
  else if (st.network == neon::client::NetworkKind::Wifi)
    net = "STA";
  netChip_.setText(net, juce::dontSendNotification);
  addr_.set("Address", st.ip.empty() ? "—" : st.ip);
  hostname_.set("Hostname", st.hostname.empty() ? "—" : st.hostname);
  trying_.set("Trying", st.wifi_ssid.empty() ? "none" : st.wifi_ssid);
  ap_.set("Access point", st.ap_ssid.empty() ? "—" : st.ap_ssid);
  peers_.set("Link peers", juce::String(static_cast<int>(st.peers)));
  const bool scanChanged =
      snap.scanning != last_snap_.scanning ||
      snap.scan_message != last_snap_.scan_message ||
      snap.scan.size() != last_snap_.scan.size();
  if (scanChanged) {
    last_snap_ = snap;
    scan_.setButtonText(snap.scanning ? "Scanning…" : "Scan");
    scan_.setEnabled(!snap.scanning);
    scanMsg_.setText(snap.scan_message, juce::dontSendNotification);
    found_.clear();
    for (const auto& s : snap.scan) {
      auto* b = found_.add(new juce::TextButton(
          juce::String(s.ssid) + " · " + juce::String(s.rssi) + "dBm" +
          (s.open ? " · open" : "")));
      neon::ui::styleBtn(*b, false);
      const std::string ssid = s.ssid;
      b->onClick = [this, ssid] {
        host_.patch([ssid](neon::Config& d) {
          int slot = -1;
          for (int i = 0; i < neon::kWifiSlots; ++i) {
            if (d.wifi[i].ssid[0] == '\0') {
              slot = i;
              break;
            }
          }
          if (slot < 0) return;
          setCstr(d.wifi[slot].ssid, sizeof(d.wifi[slot].ssid), juce::String(ssid));
          d.wifi[slot].pass[0] = '\0';
        });
      };
      addAndMakeVisible(b);
    }
    resized();
  } else {
    last_snap_ = snap;
  }
}

int NetworkPage::preferredHeight() const {
  return pane_ == 1 ? 420 + static_cast<int>(found_.size()) * 34 : 360;
}

void NetworkPage::resized() {
  neon::ui::Stack st{getLocalBounds()};
  nav_.setBounds(st.next(28));
  const bool now = pane_ == 0;
  const bool join = pane_ == 1;
  const bool ap = pane_ == 2;
  netChip_.setVisible(now);
  addr_.setVisible(now);
  hostname_.setVisible(now);
  trying_.setVisible(now);
  ap_.setVisible(now);
  peers_.setVisible(now);
  slots_.setVisible(join);
  ssid_.setVisible(join);
  pass_.setVisible(join);
  retries_.setVisible(join);
  hidden_.setVisible(join);
  scan_.setVisible(join);
  scanMsg_.setVisible(join);
  for (auto* b : found_) b->setVisible(join);
  policy_.setVisible(ap);
  apSsid_.setVisible(ap);
  apPass_.setVisible(ap);
  channel_.setVisible(ap);
  requirePass_.setVisible(ap);
  apHidden_.setVisible(ap);

  if (now) {
    netChip_.setBounds(st.next(22));
    addr_.setBounds(st.next(22));
    hostname_.setBounds(st.next(22));
    trying_.setBounds(st.next(22));
    ap_.setBounds(st.next(22));
    peers_.setBounds(st.next(22));
  } else if (join) {
    scan_.setBounds(st.next(28));
    slots_.setBounds(st.next(28));
    ssid_.setBounds(st.next(62));
    pass_.setBounds(st.next(48));
    retries_.setBounds(st.next(48));
    hidden_.setBounds(st.next(28));
    scanMsg_.setBounds(st.next(20));
    for (auto* b : found_) b->setBounds(st.next(28));
  } else {
    policy_.setBounds(st.next(48));
    apSsid_.setBounds(st.next(62));
    apPass_.setBounds(st.next(48));
    channel_.setBounds(st.next(48));
    requirePass_.setBounds(st.next(28));
    apHidden_.setBounds(st.next(28));
  }
}

// ---------------------------------------------------------------------------
// MIDI
// ---------------------------------------------------------------------------

MidiPage::MidiPage(EditorHost& host) : host_(host) {
  nav_.setItems({"BLE", "Route"}, 0);
  nav_.onChange = [this](int i) {
    pane_ = i;
    resized();
  };
  addAndMakeVisible(nav_);
  neon::ui::styleChip(bleChip_, neon::ui::magenta(), neon::ui::surface());
  addAndMakeVisible(bleChip_);
  addAndMakeVisible(bleEn_);
  addAndMakeVisible(clkOut_);
  addAndMakeVisible(transport_);
  addAndMakeVisible(channel_);
  addAndMakeVisible(gate_);
  addAndMakeVisible(policy_);
  addAndMakeVisible(pitch_);
  addAndMakeVisible(ccLat_);
  addAndMakeVisible(ccShuf_);
}

void MidiPage::load(const neon::Config& cfg) {
  bleChip_.setText(cfg.ble_enabled ? "BLE ON" : "BLE OFF", juce::dontSendNotification);
  neon::ui::styleChip(bleChip_, cfg.ble_enabled ? neon::ui::magenta() : neon::ui::muted(),
                      neon::ui::surface());
  bleEn_.setLabel("BLE MIDI enabled");
  bleEn_.setValue(cfg.ble_enabled != 0);
  bleEn_.onChange = [this](bool v) {
    host_.patch([v](neon::Config& d) { d.ble_enabled = v ? 1 : 0; });
  };
  clkOut_.setLabel("Send MIDI clock on TRS");
  clkOut_.setValue(cfg.midi_clock_out != 0);
  clkOut_.onChange = [this](bool v) {
    host_.patch([v](neon::Config& d) { d.midi_clock_out = v ? 1 : 0; });
  };
  transport_.setLabel("Follow transport messages");
  transport_.setValue(cfg.midi.transport_enabled);
  transport_.onChange = [this](bool v) {
    host_.patch([v](neon::Config& d) { d.midi.transport_enabled = v; });
  };

  std::vector<neon::ui::Option> chans{{256, "Omni (all channels)"}};
  for (int i = 0; i < 16; ++i) chans.push_back({i + 1, "Channel " + juce::String(i + 1)});
  const int chId = cfg.midi.midi_channel == 255 ? 256 : cfg.midi.midi_channel + 1;
  channel_.set("Listen on", chId, chans);
  channel_.onChange = [this](int id) {
    host_.patch([id](neon::Config& d) {
      d.midi.midi_channel = id == 256 ? 255 : static_cast<uint8_t>(id - 1);
    });
  };
  gate_.set("Notes drive",
            cfg.midi.gate_target == 255 ? 256 : cfg.midi.gate_target + 1,
            {{256, "Not routed"},
             {1, "CLK 1"},
             {2, "CLK 2"},
             {3, "CLK 3"},
             {4, "CLK 4"},
             {5, "RUN"}},
            "Mono, last note wins");
  gate_.onChange = [this](int id) {
    host_.patch([id](neon::Config& d) {
      d.midi.gate_target = id == 256 ? 255 : static_cast<uint8_t>(id - 1);
    });
  };
  policy_.set("Incoming clock", polId(cfg.midi.clock_policy), kPolicy);
  policy_.onChange = [this](int id) {
    host_.patch([id](neon::Config& d) { d.midi.clock_policy = polFrom(id); });
  };
  pitch_.setLabel("Note pitch to Tempo CV (1 V/oct)");
  pitch_.setValue(cfg.midi.pitch_cv);
  pitch_.onChange = [this](bool v) {
    host_.patch([v](neon::Config& d) { d.midi.pitch_cv = v; });
  };
  ccLat_.set("Latency CC", cfg.midi.cc_latency, 0, 255);
  ccLat_.onChange = [this](int v) {
    host_.patch([v](neon::Config& d) { d.midi.cc_latency = static_cast<uint8_t>(v); });
  };
  ccShuf_.set("Shuffle base CC", cfg.midi.cc_shuffle_base, 0, 255);
  ccShuf_.onChange = [this](int v) {
    host_.patch([v](neon::Config& d) { d.midi.cc_shuffle_base = static_cast<uint8_t>(v); });
  };
  resized();
}

int MidiPage::preferredHeight() const { return 420; }

void MidiPage::resized() {
  neon::ui::Stack st{getLocalBounds()};
  nav_.setBounds(st.next(28));
  const bool ble = pane_ == 0;
  bleChip_.setVisible(ble);
  bleEn_.setVisible(ble);
  clkOut_.setVisible(ble);
  transport_.setVisible(ble);
  channel_.setVisible(!ble);
  gate_.setVisible(!ble);
  policy_.setVisible(!ble);
  pitch_.setVisible(!ble);
  ccLat_.setVisible(!ble);
  ccShuf_.setVisible(!ble);
  if (ble) {
    bleChip_.setBounds(st.next(22));
    bleEn_.setBounds(st.next(28));
    clkOut_.setBounds(st.next(28));
    transport_.setBounds(st.next(28));
  } else {
    channel_.setBounds(st.next(48));
    gate_.setBounds(st.next(62));
    policy_.setBounds(st.next(48));
    pitch_.setBounds(st.next(28));
    st.skip(8);
    ccLat_.setBounds(st.next(48));
    ccShuf_.setBounds(st.next(48));
  }
}

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------

AudioPage::AudioPage(EditorHost& host) : host_(host) {
  nav_.setItems({"Out", "Stream"}, 0);
  nav_.onChange = [this](int i) {
    pane_ = i;
    if (i == 1) host_.send([](DeviceController& c) { c.refreshAudioChannels(); });
    resized();
  };
  addAndMakeVisible(nav_);
  banner_.setColour(juce::Label::textColourId, neon::ui::yellow());
  banner_.setFont(juce::Font(juce::FontOptions(12.0f)));
  addAndMakeVisible(banner_);
  addAndMakeVisible(enabled_);
  addAndMakeVisible(roleL_);
  addAndMakeVisible(roleR_);
  addAndMakeVisible(peakL_);
  addAndMakeVisible(peakR_);
  addAndMakeVisible(metroEn_);
  addAndMakeVisible(metroSnd_);
  addAndMakeVisible(metroGain_);
  addAndMakeVisible(metroAcc_);
  addAndMakeVisible(amyEn_);
  addAndMakeVisible(amyPatch_);
  addAndMakeVisible(amyGain_);
  addAndMakeVisible(lineMon_);
  addAndMakeVisible(pubMix_);
  addAndMakeVisible(pubLine_);
  addAndMakeVisible(pubMono_);
  addAndMakeVisible(chName_);
  pubNote_.setColour(juce::Label::textColourId, neon::ui::muted());
  pubNote_.setFont(juce::Font(juce::FontOptions(12.0f)));
  addAndMakeVisible(pubNote_);
  addAndMakeVisible(subCh_);
  addAndMakeVisible(jitter_);
  addAndMakeVisible(subGain_);
  neon::ui::styleBtn(refresh_, false);
  refresh_.onClick = [this] {
    host_.send([](DeviceController& c) { c.refreshAudioChannels(); });
  };
  addAndMakeVisible(refresh_);
  subNote_.setColour(juce::Label::textColourId, neon::ui::muted());
  addAndMakeVisible(subNote_);
}

void AudioPage::load(const neon::Config& cfg, const Snapshot& snap) {
  cfg_ = cfg;
  const auto& a = cfg.audio;
  const auto& s = snap.status.audio;
  banner_.setText(a.enabled && !s.running
                      ? "Audio is not running. Save, reboot, and check the console."
                      : (!snap.audio_channels.available && pane_ == 1
                             ? "Streaming is not in this firmware."
                             : ""),
                  juce::dontSendNotification);
  enabled_.setLabel("Audio engine enabled");
  enabled_.setValue(a.enabled != 0);
  enabled_.onChange = [this](bool v) {
    host_.patch([v](neon::Config& d) { d.audio.enabled = v ? 1 : 0; });
  };
  roleL_.set("Left carries", audioId(a.role_l), kAudioRoles);
  roleL_.onChange = [this](int id) {
    host_.patch([id](neon::Config& d) { d.audio.role_l = audioFrom(id); });
  };
  roleR_.set("Right carries", audioId(a.role_r), kAudioRoles);
  roleR_.onChange = [this](int id) {
    host_.patch([id](neon::Config& d) { d.audio.role_r = audioFrom(id); });
  };
  peakL_.set("L", static_cast<int>(s.peak_l));
  peakR_.set("R", static_cast<int>(s.peak_r));
  metroEn_.setLabel("Click on every beat");
  metroEn_.setValue(a.metro_enabled != 0);
  metroEn_.onChange = [this](bool v) {
    host_.patch([v](neon::Config& d) { d.audio.metro_enabled = v ? 1 : 0; });
  };
  metroSnd_.set("Sound", a.metro_sound == ClickSound::kNoise ? 2
                         : a.metro_sound == ClickSound::kWood  ? 3
                                                               : 1,
                {{1, "Sine"}, {2, "Noise"}, {3, "Wood"}});
  metroSnd_.onChange = [this](int id) {
    host_.patch([id](neon::Config& d) {
      d.audio.metro_sound = id == 2   ? ClickSound::kNoise
                            : id == 3 ? ClickSound::kWood
                                      : ClickSound::kSine;
    });
  };
  metroGain_.set("Level %", toPct(a.metro_gain), 0, 127);
  metroGain_.onChange = [this](int v) {
    host_.patch([v](neon::Config& d) { d.audio.metro_gain = static_cast<uint8_t>(fromPct(v)); });
  };
  metroAcc_.setLabel("Accent the downbeat");
  metroAcc_.setValue(a.metro_accent != 0);
  metroAcc_.onChange = [this](bool v) {
    host_.patch([v](neon::Config& d) { d.audio.metro_accent = v ? 1 : 0; });
  };
  amyEn_.setLabel("Synth voice enabled");
  amyEn_.setValue(a.amy_enabled != 0);
  amyEn_.onChange = [this](bool v) {
    host_.patch([v](neon::Config& d) { d.audio.amy_enabled = v ? 1 : 0; });
  };
  amyPatch_.set("Patch", a.amy_patch, 0, 3);
  amyPatch_.onChange = [this](int v) {
    host_.patch([v](neon::Config& d) { d.audio.amy_patch = static_cast<uint8_t>(v); });
  };
  amyGain_.set("Level %", toPct(a.amy_gain), 0, 127);
  amyGain_.onChange = [this](int v) {
    host_.patch([v](neon::Config& d) { d.audio.amy_gain = static_cast<uint8_t>(fromPct(v)); });
  };
  lineMon_.set("Monitor %", toPct(a.linein_monitor_gain), 0, 127);
  lineMon_.onChange = [this](int v) {
    host_.patch([v](neon::Config& d) {
      d.audio.linein_monitor_gain = static_cast<uint8_t>(fromPct(v));
    });
  };
  pubMix_.setLabel("Publish the mix");
  pubMix_.setValue(a.la_publish_mix != 0);
  pubMix_.onChange = [this](bool v) {
    host_.patch([v](neon::Config& d) { d.audio.la_publish_mix = v ? 1 : 0; });
  };
  pubLine_.setLabel("Publish the line input");
  pubLine_.setValue(a.la_publish_linein != 0);
  pubLine_.onChange = [this](bool v) {
    host_.patch([v](neon::Config& d) { d.audio.la_publish_linein = v ? 1 : 0; });
  };
  pubMono_.setLabel("Mono (halves the bitrate)");
  pubMono_.setValue(a.la_publish_mono != 0);
  pubMono_.onChange = [this](bool v) {
    host_.patch([v](neon::Config& d) { d.audio.la_publish_mono = v ? 1 : 0; });
  };
  chName_.set("Channel name", juce::String::fromUTF8(a.la_channel_name), 23, {}, false,
              juce::String::fromUTF8(cfg.device_name));
  chName_.onChange = [this](juce::String v) {
    host_.patch([v](neon::Config& d) {
      setCstr(d.audio.la_channel_name, sizeof(d.audio.la_channel_name), v);
    });
  };
  const juce::String base = a.la_channel_name[0] ? juce::String::fromUTF8(a.la_channel_name)
                                                 : juce::String::fromUTF8(cfg.device_name);
  pubNote_.setText("Published as “" + base + " Out”" +
                       (a.la_publish_linein ? " and “" + base + " In”" : "") + ".",
                   juce::dontSendNotification);

  std::vector<neon::ui::Option> chs{{1, "Not subscribed"}};
  int sel = 1;
  int id = 2;
  const juce::String cur = juce::String::fromUTF8(a.la_sub_channel_id);
  bool found = cur.isEmpty();
  for (const auto& ch : snap.audio_channels.channels) {
    if (ch.local) continue;
    const juce::String lab = ch.peer.empty()
                                 ? juce::String(ch.name)
                                 : juce::String(ch.peer) + " / " + juce::String(ch.name);
    chs.push_back({id, lab});
    if (cur == juce::String(ch.id)) {
      sel = id;
      found = true;
    }
    ++id;
  }
  if (!found && cur.isNotEmpty()) {
    chs.push_back({id, cur + " (offline)"});
    sel = id;
  }
  subCh_.set("Channel", sel, chs);
  subCh_.onChange = [this, snap](int picked) {
    juce::String chosen;
    int n = 2;
    for (const auto& ch : snap.audio_channels.channels) {
      if (ch.local) continue;
      if (n == picked) {
        chosen = juce::String(ch.id);
        break;
      }
      ++n;
    }
    host_.patch([chosen](neon::Config& d) {
      setCstr(d.audio.la_sub_channel_id, sizeof(d.audio.la_sub_channel_id), chosen);
    });
  };
  jitter_.set("Buffer ms", a.la_jitter_ms, 5, 800, 5);
  jitter_.onChange = [this](int v) {
    host_.patch([v](neon::Config& d) { d.audio.la_jitter_ms = static_cast<uint16_t>(v); });
  };
  subGain_.set("Level %", toPct(a.la_sub_gain), 0, 127);
  subGain_.onChange = [this](int v) {
    host_.patch([v](neon::Config& d) { d.audio.la_sub_gain = static_cast<uint8_t>(fromPct(v)); });
  };
  refresh_.setButtonText(snap.audio_refreshing ? "Looking…" : "Refresh");
  juce::String note = "Set the left or right jack to “Link Audio in” to hear it.";
  if (a.la_sub_channel_id[0] != '\0') {
    if (s.sub_state == neon::client::SubState::Playing)
      note = "Playing at " + juce::String(s.sub_rate / 1000.0, 1) + " kHz";
    else if (s.sub_state == neon::client::SubState::Buffering)
      note = "Buffering…";
    else
      note = "Waiting for audio";
    if (s.fill_ms) note += " · fill " + juce::String(static_cast<int>(s.fill_ms)) + " ms";
    if (s.sub_dropped) note += " · " + juce::String(static_cast<int>(s.sub_dropped)) + " dropped";
  }
  subNote_.setText(note, juce::dontSendNotification);
  resized();
}

void AudioPage::tickStatus(const Snapshot& snap) {
  const auto& s = snap.status.audio;
  peakL_.set("L", static_cast<int>(s.peak_l));
  peakR_.set("R", static_cast<int>(s.peak_r));
  refresh_.setButtonText(snap.audio_refreshing ? "Looking…" : "Refresh");
}

int AudioPage::preferredHeight() const { return pane_ == 0 ? 620 : 480; }

void AudioPage::resized() {
  neon::ui::Stack st{getLocalBounds()};
  nav_.setBounds(st.next(28));
  const bool out = pane_ == 0;
  banner_.setVisible(banner_.getText().isNotEmpty());
  if (banner_.isVisible()) banner_.setBounds(st.next(32));
  enabled_.setVisible(out);
  roleL_.setVisible(out);
  roleR_.setVisible(out);
  peakL_.setVisible(out);
  peakR_.setVisible(out);
  metroEn_.setVisible(out);
  metroSnd_.setVisible(out);
  metroGain_.setVisible(out);
  metroAcc_.setVisible(out);
  amyEn_.setVisible(out);
  amyPatch_.setVisible(out);
  amyGain_.setVisible(out);
  lineMon_.setVisible(out);
  pubMix_.setVisible(!out);
  pubLine_.setVisible(!out);
  pubMono_.setVisible(!out);
  chName_.setVisible(!out);
  pubNote_.setVisible(!out);
  subCh_.setVisible(!out);
  jitter_.setVisible(!out);
  subGain_.setVisible(!out);
  refresh_.setVisible(!out);
  subNote_.setVisible(!out);
  if (out) {
    enabled_.setBounds(st.next(28));
    roleL_.setBounds(st.next(48));
    roleR_.setBounds(st.next(48));
    peakL_.setBounds(st.next(16));
    peakR_.setBounds(st.next(16));
    st.skip(6);
    metroEn_.setBounds(st.next(28));
    metroSnd_.setBounds(st.next(48));
    metroGain_.setBounds(st.next(48));
    metroAcc_.setBounds(st.next(28));
    st.skip(6);
    amyEn_.setBounds(st.next(28));
    amyPatch_.setBounds(st.next(48));
    amyGain_.setBounds(st.next(48));
    st.skip(6);
    lineMon_.setBounds(st.next(48));
  } else {
    pubMix_.setBounds(st.next(28));
    pubLine_.setBounds(st.next(28));
    pubMono_.setBounds(st.next(28));
    chName_.setBounds(st.next(48));
    pubNote_.setBounds(st.next(28));
    refresh_.setBounds(st.next(28));
    subCh_.setBounds(st.next(48));
    jitter_.setBounds(st.next(48));
    subGain_.setBounds(st.next(48));
    subNote_.setBounds(st.next(36));
  }
}

// ---------------------------------------------------------------------------
// System
// ---------------------------------------------------------------------------

SystemPage::SystemPage(EditorHost& host) : host_(host) {
  nav_.setItems({"Time", "Clock", "Panel", "Module"}, 0);
  nav_.onChange = [this](int i) {
    pane_ = i;
    resized();
  };
  addAndMakeVisible(nav_);
  addAndMakeVisible(latency_);
  addAndMakeVisible(quantum_);
  addAndMakeVisible(resetMode_);
  addAndMakeVisible(resetLen_);
  addAndMakeVisible(midiNudge_);
  addAndMakeVisible(resetLead_);
  addAndMakeVisible(gating_);
  addAndMakeVisible(resetLeadEn_);
  addAndMakeVisible(startStop_);
  addAndMakeVisible(clkSrc_);
  addAndMakeVisible(clkIn_);
  addAndMakeVisible(cvMin_);
  addAndMakeVisible(cvMax_);
  addAndMakeVisible(name_);
  addAndMakeVisible(bright_);
  addAndMakeVisible(bigBeat_);
  addAndMakeVisible(fw_);
  addAndMakeVisible(hostname_);
  addAndMakeVisible(ip_);
  neon::ui::styleDanger(reboot_);
  neon::ui::styleDanger(factory_);
  reboot_.onClick = [this] {
    if (onReboot) onReboot();
  };
  factory_.onClick = [this] {
    if (onFactoryReset) onFactoryReset();
  };
  addAndMakeVisible(reboot_);
  addAndMakeVisible(factory_);
}

void SystemPage::load(const neon::Config& cfg, const Snapshot& snap) {
  latency_.set("Latency", cfg.engine.latency_us, -50000, 50000, 100, "µs");
  latency_.onChange = [this](int v) {
    host_.patch([v](neon::Config& d) { d.engine.latency_us = v; });
  };
  quantum_.set("Quantum", static_cast<int>(cfg.quantum_beats), 1, 16, 1, "Beats per bar");
  quantum_.onChange = [this](int v) {
    host_.patch([v](neon::Config& d) { d.quantum_beats = static_cast<uint32_t>(v); });
  };
  const int rm = cfg.engine.reset_mode == ResetMode::kEveryBar     ? 2
                 : cfg.engine.reset_mode == ResetMode::kAtStop     ? 3
                 : cfg.engine.reset_mode == ResetMode::kOff        ? 4
                                                                   : 1;
  resetMode_.set("Reset pulse", rm,
                 {{1, "On start of play"},
                  {2, "Every bar"},
                  {3, "On stop of play"},
                  {4, "Never"}});
  resetMode_.onChange = [this](int id) {
    host_.patch([id](neon::Config& d) {
      d.engine.reset_mode = id == 2   ? ResetMode::kEveryBar
                            : id == 3 ? ResetMode::kAtStop
                            : id == 4 ? ResetMode::kOff
                                      : ResetMode::kStartOfPlay;
    });
  };
  resetLen_.set("Reset length", static_cast<int>(cfg.engine.reset_trig_len_us), 100, 100000, 100,
                "µs");
  resetLen_.onChange = [this](int v) {
    host_.patch([v](neon::Config& d) { d.engine.reset_trig_len_us = static_cast<uint32_t>(v); });
  };
  midiNudge_.set("MIDI nudge", cfg.midi_nudge_us, -100000, 100000, 500,
                 "µs — MIDI only, independent of the latency above");
  midiNudge_.onChange = [this](int v) {
    host_.patch([v](neon::Config& d) { d.midi_nudge_us = v; });
  };
  resetLead_.set("Reset lead", static_cast<int>(cfg.engine.reset_lead_us), 0, 50000, 100,
                 "µs, when leading is on");
  resetLead_.onChange = [this](int v) {
    host_.patch([v](neon::Config& d) { d.engine.reset_lead_us = static_cast<uint32_t>(v); });
  };
  gating_.setLabel("Stop clock outputs when the transport stops");
  gating_.setValue(cfg.engine.transport_gating);
  gating_.onChange = [this](bool v) {
    host_.patch([v](neon::Config& d) { d.engine.transport_gating = v; });
  };
  resetLeadEn_.setLabel("Reset leads the clock edge");
  resetLeadEn_.setValue(cfg.engine.reset_before_edge);
  resetLeadEn_.onChange = [this](bool v) {
    host_.patch([v](neon::Config& d) { d.engine.reset_before_edge = v; });
  };
  startStop_.setLabel("Follow Link start/stop from other peers");
  startStop_.setValue(cfg.start_stop_sync != 0);
  startStop_.onChange = [this](bool v) {
    host_.patch([v](neon::Config& d) { d.start_stop_sync = v ? 1 : 0; });
  };
  clkSrc_.set("Source", srcId(cfg.clock_source),
              {{1, "Auto"}, {2, "Link is the master"}, {3, "External input is the master"}});
  clkSrc_.onChange = [this](int id) {
    host_.patch([id](neon::Config& d) { d.clock_source = srcFrom(id); });
  };
  clkIn_.set("CLK IN rate", static_cast<int>(cfg.clock_in_ppqn), 1, 96, 1,
             "Pulses per quarter note");
  clkIn_.onChange = [this](int v) {
    host_.patch([v](neon::Config& d) { d.clock_in_ppqn = static_cast<uint32_t>(v); });
  };
  cvMin_.set("Minimum", cfg.tempo_cv_min_bpm, 1, 998, 1, "BPM at 0 V");
  cvMin_.onChange = [this](int v) {
    host_.patch([v](neon::Config& d) { d.tempo_cv_min_bpm = static_cast<uint16_t>(v); });
  };
  cvMax_.set("Maximum", cfg.tempo_cv_max_bpm, 2, 999, 1, "BPM at 5 V");
  cvMax_.onChange = [this](int v) {
    host_.patch([v](neon::Config& d) { d.tempo_cv_max_bpm = static_cast<uint16_t>(v); });
  };
  name_.set("Device name", juce::String::fromUTF8(cfg.device_name), 23,
            "http://" + juce::String::fromUTF8(cfg.device_name[0] ? cfg.device_name : "neon-link") +
                ".local/");
  name_.onChange = [this](juce::String v) {
    host_.patch([v](neon::Config& d) { setCstr(d.device_name, sizeof(d.device_name), v); });
  };
  bright_.set("Display brightness", cfg.display_brightness, 0, 255, 1, "0 blanks the panel");
  bright_.onChange = [this](int v) {
    host_.patch([v](neon::Config& d) { d.display_brightness = static_cast<uint8_t>(v); });
  };
  bigBeat_.setLabel("Big beat numbers");
  bigBeat_.setValue(cfg.big_beat_display != 0);
  bigBeat_.onChange = [this](bool v) {
    host_.patch([v](neon::Config& d) { d.big_beat_display = v ? 1 : 0; });
  };
  fw_.set("Installed", snap.status.firmware.empty() ? "—" : snap.status.firmware);
  hostname_.set("Hostname", snap.status.hostname.empty() ? "—" : snap.status.hostname);
  ip_.set("Address", snap.status.ip.empty() ? "—" : snap.status.ip);
  resized();
}

int SystemPage::preferredHeight() const { return 520; }

void SystemPage::resized() {
  neon::ui::Stack st{getLocalBounds()};
  nav_.setBounds(st.next(28));
  const bool time = pane_ == 0;
  const bool clock = pane_ == 1;
  const bool panel = pane_ == 2;
  const bool mod = pane_ == 3;
  latency_.setVisible(time);
  quantum_.setVisible(time);
  resetMode_.setVisible(time);
  resetLen_.setVisible(time);
  midiNudge_.setVisible(time);
  resetLead_.setVisible(time);
  gating_.setVisible(time);
  resetLeadEn_.setVisible(time);
  startStop_.setVisible(time);
  clkSrc_.setVisible(clock);
  clkIn_.setVisible(clock);
  cvMin_.setVisible(clock);
  cvMax_.setVisible(clock);
  name_.setVisible(panel);
  bright_.setVisible(panel);
  bigBeat_.setVisible(panel);
  fw_.setVisible(mod);
  hostname_.setVisible(mod);
  ip_.setVisible(mod);
  reboot_.setVisible(mod);
  factory_.setVisible(mod);
  if (time) {
    latency_.setBounds(st.next(62));
    quantum_.setBounds(st.next(62));
    resetMode_.setBounds(st.next(48));
    resetLen_.setBounds(st.next(62));
    midiNudge_.setBounds(st.next(62));
    resetLead_.setBounds(st.next(62));
    gating_.setBounds(st.next(28));
    resetLeadEn_.setBounds(st.next(28));
    startStop_.setBounds(st.next(28));
  } else if (clock) {
    clkSrc_.setBounds(st.next(48));
    clkIn_.setBounds(st.next(62));
    cvMin_.setBounds(st.next(62));
    cvMax_.setBounds(st.next(62));
  } else if (panel) {
    name_.setBounds(st.next(62));
    bright_.setBounds(st.next(62));
    bigBeat_.setBounds(st.next(28));
  } else {
    fw_.setBounds(st.next(22));
    hostname_.setBounds(st.next(22));
    ip_.setBounds(st.next(22));
    auto row = st.next(32);
    reboot_.setBounds(row.removeFromLeft(120));
    row.removeFromLeft(8);
    factory_.setBounds(row.removeFromLeft(140));
  }
}

}  // namespace neon::plugin
