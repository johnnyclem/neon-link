#include "Form.h"

namespace neon::ui {
namespace {

juce::Font labelFont() {
  return juce::Font(juce::FontOptions(11.0f)).withExtraKerningFactor(0.08f);
}

}  // namespace

NeonLookAndFeel::NeonLookAndFeel() {
  setColour(juce::ComboBox::backgroundColourId, surface());
  setColour(juce::ComboBox::textColourId, text());
  setColour(juce::ComboBox::outlineColourId, border());
  setColour(juce::ComboBox::focusedOutlineColourId, neon());
  setColour(juce::ComboBox::arrowColourId, muted());
  setColour(juce::PopupMenu::backgroundColourId, surface2());
  setColour(juce::PopupMenu::textColourId, text());
  setColour(juce::PopupMenu::highlightedBackgroundColourId, neon());
  setColour(juce::PopupMenu::highlightedTextColourId, bg());
  setColour(juce::PopupMenu::headerTextColourId, muted());
  setColour(juce::ScrollBar::thumbColourId, border());
  setColour(juce::CaretComponent::caretColourId, neon());
  setColour(juce::TextEditor::highlightedTextColourId, bg());
}

void styleChip(juce::Label& l, juce::Colour fg, juce::Colour bgc) {
  l.setColour(juce::Label::textColourId, fg);
  l.setColour(juce::Label::backgroundColourId, bgc);
  l.setJustificationType(juce::Justification::centred);
  l.setFont(juce::Font(juce::FontOptions(11.0f)).withExtraKerningFactor(0.12f));
}

void styleBtn(juce::TextButton& b, bool primary) {
  b.setColour(juce::TextButton::buttonColourId,
              primary ? neon() : surface2());
  b.setColour(juce::TextButton::textColourOffId, primary ? bg() : text());
  b.setColour(juce::TextButton::buttonOnColourId, neonDim());
  b.setColour(juce::TextButton::textColourOnId, bg());
}

void styleDanger(juce::TextButton& b) {
  b.setColour(juce::TextButton::buttonColourId, danger());
  b.setColour(juce::TextButton::textColourOffId, bg());
}

void styleField(juce::TextEditor& e) {
  e.setColour(juce::TextEditor::backgroundColourId, surface());
  e.setColour(juce::TextEditor::textColourId, text());
  e.setColour(juce::TextEditor::outlineColourId, border());
  e.setColour(juce::TextEditor::focusedOutlineColourId, neon());
  e.setColour(juce::TextEditor::highlightColourId, neonDim());
  e.setColour(juce::TextEditor::highlightedTextColourId, bg());
  e.setFont(juce::Font(juce::FontOptions(14.0f)));
}

void styleCombo(juce::ComboBox& c) {
  c.setColour(juce::ComboBox::backgroundColourId, surface());
  c.setColour(juce::ComboBox::textColourId, text());
  c.setColour(juce::ComboBox::outlineColourId, border());
  c.setColour(juce::ComboBox::focusedOutlineColourId, neon());
  c.setColour(juce::ComboBox::arrowColourId, muted());
}

Toggle::Toggle() {
  label_.setColour(juce::Label::textColourId, text());
  label_.setFont(juce::Font(juce::FontOptions(13.0f)));
  addAndMakeVisible(label_);
  styleBtn(btn_, false);
  btn_.onClick = [this] {
    on_ = !on_;
    btn_.setButtonText(on_ ? "ON" : "OFF");
    styleBtn(btn_, on_);
    if (onChange) onChange(on_);
  };
  addAndMakeVisible(btn_);
}

void Toggle::setLabel(const juce::String& s) { label_.setText(s, juce::dontSendNotification); }

void Toggle::setValue(bool v) {
  on_ = v;
  btn_.setButtonText(v ? "ON" : "OFF");
  styleBtn(btn_, v);
}

void Toggle::resized() {
  auto r = getLocalBounds();
  btn_.setBounds(r.removeFromRight(56));
  r.removeFromRight(8);
  label_.setBounds(r);
}

void Toggle::paint(juce::Graphics&) {}

NumberField::NumberField() {
  label_.setColour(juce::Label::textColourId, muted());
  label_.setFont(labelFont());
  addAndMakeVisible(label_);
  styleField(edit_);
  edit_.setInputRestrictions(12, "-0123456789");
  edit_.addListener(this);
  addAndMakeVisible(edit_);
  hint_.setColour(juce::Label::textColourId, muted());
  hint_.setFont(juce::Font(juce::FontOptions(11.0f)));
  addAndMakeVisible(hint_);
}

void NumberField::set(const juce::String& lab, int value, int mn, int mx,
                      int /*step*/, const juce::String& hint) {
  label_.setText(lab, juce::dontSendNotification);
  min_ = mn;
  max_ = mx;
  hint_.setText(hint, juce::dontSendNotification);
  hint_.setVisible(hint.isNotEmpty());
  setValue(value);
}

void NumberField::setValue(int v) {
  last_ = v;
  if (isEditing()) return;
  edit_.setText(juce::String(v), juce::dontSendNotification);
}

bool NumberField::isEditing() const { return edit_.hasKeyboardFocus(true); }

void NumberField::resized() {
  auto r = getLocalBounds();
  label_.setBounds(r.removeFromTop(14));
  if (hint_.isVisible()) {
    hint_.setBounds(r.removeFromBottom(14));
  }
  edit_.setBounds(r);
}

void NumberField::textEditorReturnKeyPressed(juce::TextEditor&) { commit(); }

void NumberField::textEditorFocusLost(juce::TextEditor&) { commit(); }

void NumberField::commit() {
  int v = edit_.getText().getIntValue();
  v = juce::jlimit(min_, max_, v);
  edit_.setText(juce::String(v), juce::dontSendNotification);
  if (v != last_ && onChange) {
    last_ = v;
    onChange(v);
  }
}

TextField::TextField() {
  label_.setColour(juce::Label::textColourId, muted());
  label_.setFont(labelFont());
  addAndMakeVisible(label_);
  styleField(edit_);
  edit_.addListener(this);
  addAndMakeVisible(edit_);
  hint_.setColour(juce::Label::textColourId, muted());
  hint_.setFont(juce::Font(juce::FontOptions(11.0f)));
  addAndMakeVisible(hint_);
}

void TextField::set(const juce::String& lab, const juce::String& value,
                    int maxLen, const juce::String& hint, bool password,
                    const juce::String& placeholder) {
  label_.setText(lab, juce::dontSendNotification);
  hint_.setText(hint, juce::dontSendNotification);
  hint_.setVisible(hint.isNotEmpty());
  edit_.setInputRestrictions(maxLen);
  edit_.setPasswordCharacter(password ? 0x2022 : 0);
  edit_.setTextToShowWhenEmpty(placeholder, muted());
  setValue(value);
}

void TextField::setValue(const juce::String& v) {
  if (isEditing()) return;
  suppress_ = true;
  edit_.setText(v, juce::dontSendNotification);
  suppress_ = false;
}

bool TextField::isEditing() const { return edit_.hasKeyboardFocus(true); }

void TextField::resized() {
  auto r = getLocalBounds();
  label_.setBounds(r.removeFromTop(14));
  if (hint_.isVisible()) {
    hint_.setBounds(r.removeFromBottom(14));
  }
  edit_.setBounds(r);
}

void TextField::textEditorTextChanged(juce::TextEditor&) {
  if (!suppress_ && onChange) onChange(edit_.getText());
}

void TextField::textEditorFocusLost(juce::TextEditor&) {
  if (onChange) onChange(edit_.getText());
}

SelectField::SelectField() {
  label_.setColour(juce::Label::textColourId, muted());
  label_.setFont(labelFont());
  addAndMakeVisible(label_);
  styleCombo(box_);
  box_.onChange = [this] {
    if (onChange) onChange(box_.getSelectedId());
  };
  addAndMakeVisible(box_);
  hint_.setColour(juce::Label::textColourId, muted());
  hint_.setFont(juce::Font(juce::FontOptions(11.0f)));
  addAndMakeVisible(hint_);
}

void SelectField::set(const juce::String& lab, int selectedId,
                      const std::vector<Option>& options,
                      const juce::String& hint) {
  label_.setText(lab, juce::dontSendNotification);
  hint_.setText(hint, juce::dontSendNotification);
  hint_.setVisible(hint.isNotEmpty());
  box_.clear(juce::dontSendNotification);
  for (const auto& o : options) box_.addItem(o.label, o.id);
  box_.setSelectedId(selectedId, juce::dontSendNotification);
}

void SelectField::setSelected(int id) {
  if (box_.getSelectedId() == id) return;
  box_.setSelectedId(id, juce::dontSendNotification);
}

void SelectField::resized() {
  auto r = getLocalBounds();
  label_.setBounds(r.removeFromTop(14));
  if (hint_.isVisible()) {
    hint_.setBounds(r.removeFromBottom(14));
  }
  box_.setBounds(r);
}

Readout::Readout() {
  label_.setColour(juce::Label::textColourId, muted());
  label_.setFont(labelFont());
  value_.setColour(juce::Label::textColourId, text());
  value_.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::plain))
                     .withTypefaceStyle("Regular"));
  value_.setJustificationType(juce::Justification::centredRight);
  addAndMakeVisible(label_);
  addAndMakeVisible(value_);
}

void Readout::set(const juce::String& lab, const juce::String& val) {
  label_.setText(lab, juce::dontSendNotification);
  value_.setText(val, juce::dontSendNotification);
}

void Readout::resized() {
  auto r = getLocalBounds();
  value_.setBounds(r.removeFromRight(juce::jmax(80, r.getWidth() / 2)));
  label_.setBounds(r);
}

void SubNav::setItems(const std::vector<juce::String>& items, int selected) {
  if (static_cast<int>(items.size()) == btns_.size() && selected_ == selected) {
    bool same = true;
    for (int i = 0; i < btns_.size(); ++i) {
      if (btns_[i]->getButtonText() != items[static_cast<size_t>(i)]) {
        same = false;
        break;
      }
    }
    if (same) return;
  }
  btns_.clear();
  selected_ = selected;
  for (int i = 0; i < static_cast<int>(items.size()); ++i) {
    auto* b = btns_.add(new juce::TextButton(items[static_cast<size_t>(i)]));
    styleBtn(*b, i == selected_);
    b->onClick = [this, i] {
      selected_ = i;
      for (int j = 0; j < btns_.size(); ++j) styleBtn(*btns_[j], j == selected_);
      if (onChange) onChange(i);
    };
    addAndMakeVisible(b);
  }
  resized();
}

void SubNav::resized() {
  if (btns_.isEmpty()) return;
  auto r = getLocalBounds();
  const int n = btns_.size();
  const int gap = 4;
  const int w = (r.getWidth() - gap * (n - 1)) / n;
  for (int i = 0; i < n; ++i) {
    btns_[i]->setBounds(r.removeFromLeft(w));
    r.removeFromLeft(gap);
  }
}

void TabBar::setTabs(const std::vector<juce::String>& tabs, int selected) {
  if (btns_.isEmpty()) {
    for (int i = 0; i < static_cast<int>(tabs.size()); ++i) {
      auto* b = btns_.add(new juce::TextButton(tabs[static_cast<size_t>(i)]));
      b->onClick = [this, i] {
        selected_ = i;
        for (int j = 0; j < btns_.size(); ++j) styleBtn(*btns_[j], j == selected_);
        if (onChange) onChange(i);
      };
      addAndMakeVisible(b);
    }
  }
  selected_ = selected;
  for (int j = 0; j < btns_.size(); ++j) styleBtn(*btns_[j], j == selected_);
}

void TabBar::resized() {
  auto r = getLocalBounds().reduced(0, 4);
  const int n = btns_.size();
  if (n == 0) return;
  const int gap = 4;
  const int w = (r.getWidth() - gap * (n - 1)) / n;
  for (int i = 0; i < n; ++i) {
    btns_[i]->setBounds(r.removeFromLeft(w));
    r.removeFromLeft(gap);
  }
}

void TabBar::paint(juce::Graphics& g) {
  g.setColour(surface());
  g.fillRect(getLocalBounds());
  g.setColour(border());
  g.drawRect(getLocalBounds(), 1);
}

void StepGrid::set(int steps, uint64_t mask) {
  steps = juce::jlimit(1, 64, steps);
  if (steps_ != steps) {
    cells_.clear();
    steps_ = steps;
    for (int i = 0; i < steps_; ++i) {
      auto* b = cells_.add(new juce::TextButton());
      b->setClickingTogglesState(true);
      b->onClick = [this, i] {
        if (onToggle) onToggle(i);
      };
      addAndMakeVisible(b);
    }
  }
  mask_ = mask;
  for (int i = 0; i < cells_.size(); ++i) {
    const bool on = (mask_ >> i) & 1ull;
    cells_[i]->setToggleState(on, juce::dontSendNotification);
    cells_[i]->setColour(juce::TextButton::buttonColourId,
                         on ? neon() : surface2());
    cells_[i]->setColour(juce::TextButton::buttonOnColourId, neon());
    cells_[i]->setColour(juce::TextButton::textColourOffId,
                         i % 4 == 0 ? muted() : text());
    cells_[i]->setButtonText(i % 4 == 0 ? juce::String(i + 1) : juce::String());
  }
  resized();
}

void StepGrid::resized() {
  if (cells_.isEmpty()) return;
  auto r = getLocalBounds();
  const int cols = 16;
  const int rows = (steps_ + cols - 1) / cols;
  const int gap = 3;
  const int cw = (r.getWidth() - gap * (cols - 1)) / cols;
  const int ch = rows > 0 ? (r.getHeight() - gap * (rows - 1)) / rows : 18;
  for (int i = 0; i < cells_.size(); ++i) {
    const int col = i % cols;
    const int row = i / cols;
    cells_[i]->setBounds(col * (cw + gap), row * (ch + gap), cw, ch);
  }
}

void Meter::set(const juce::String& lab, int milli) {
  label_ = lab;
  milli_ = juce::jlimit(0, 1000, milli);
  repaint();
}

void Meter::paint(juce::Graphics& g) {
  auto r = getLocalBounds();
  g.setColour(muted());
  g.setFont(juce::Font(juce::FontOptions(11.0f)));
  g.drawText(label_, r.removeFromLeft(16), juce::Justification::centredLeft);
  r.removeFromLeft(6);
  g.setColour(surface2());
  g.fillRect(r);
  g.setColour(border());
  g.drawRect(r, 1);
  auto fill = r.reduced(2);
  fill.setWidth(juce::roundToInt(fill.getWidth() * (milli_ / 1000.0f)));
  g.setColour(milli_ > 900 ? danger() : neon());
  g.fillRect(fill);
}

Confirm::Confirm() {
  title_.setColour(juce::Label::textColourId, text());
  title_.setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold)));
  body_.setColour(juce::Label::textColourId, text());
  body_.setFont(juce::Font(juce::FontOptions(13.0f)));
  body_.setMinimumHorizontalScale(1.0f);
  addAndMakeVisible(title_);
  addAndMakeVisible(body_);
  styleDanger(ok_);
  styleBtn(cancel_, false);
  ok_.onClick = [this] {
    if (onConfirm) onConfirm();
  };
  cancel_.onClick = [this] {
    if (onCancel) onCancel();
  };
  addAndMakeVisible(ok_);
  addAndMakeVisible(cancel_);
}

void Confirm::set(const juce::String& t, const juce::String& b,
                  const juce::String& confirm) {
  title_.setText(t, juce::dontSendNotification);
  body_.setText(b, juce::dontSendNotification);
  ok_.setButtonText(confirm);
}

void Confirm::resized() {
  auto card = getLocalBounds().reduced(40).withSizeKeepingCentre(
      juce::jmin(420, getWidth() - 40), 200);
  auto r = card.reduced(16);
  title_.setBounds(r.removeFromTop(24));
  r.removeFromTop(8);
  auto btns = r.removeFromBottom(32);
  ok_.setBounds(btns.removeFromLeft(140));
  btns.removeFromLeft(8);
  cancel_.setBounds(btns.removeFromLeft(100));
  body_.setBounds(r);
}

void Confirm::paint(juce::Graphics& g) {
  g.fillAll(juce::Colour(0xcc0b0c0f));
  auto card = getLocalBounds().reduced(40).withSizeKeepingCentre(
      juce::jmin(420, getWidth() - 40), 200);
  g.setColour(surface());
  g.fillRect(card);
  g.setColour(danger());
  g.drawRect(card, 1);
}

}  // namespace neon::ui
