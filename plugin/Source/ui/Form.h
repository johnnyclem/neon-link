#pragma once

#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Tokens.h"

namespace neon::ui {

class NeonLookAndFeel : public juce::LookAndFeel_V4 {
 public:
  NeonLookAndFeel();

  // Live (and some other hosts) deadlock if a ComboBox popup is a new
  // desktop window. Keep the menu inside the editor peer.
  juce::Component* getParentComponentForMenuOptions(
      const juce::PopupMenu::Options& options) override {
    if (auto* target = options.getTargetComponent()) {
      if (auto* top = target->getTopLevelComponent()) {
        return top;
      }
    }
    if (auto* parent = options.getParentComponent()) {
      return parent;
    }
    return juce::LookAndFeel_V4::getParentComponentForMenuOptions(options);
  }
};

void styleChip(juce::Label&, juce::Colour fg, juce::Colour bg);
void styleBtn(juce::TextButton&, bool primary);
void styleDanger(juce::TextButton&);
void styleField(juce::TextEditor&);
void styleCombo(juce::ComboBox&);

struct Option {
  int id = 0;
  juce::String label;
};

class Toggle : public juce::Component {
 public:
  std::function<void(bool)> onChange;

  Toggle();
  void setLabel(const juce::String&);
  void setValue(bool);
  bool value() const { return on_; }
  void resized() override;
  void paint(juce::Graphics&) override;

 private:
  juce::Label label_;
  juce::TextButton btn_{"OFF"};
  bool on_ = false;
};

class NumberField : public juce::Component, private juce::TextEditor::Listener {
 public:
  std::function<void(int)> onChange;

  NumberField();
  void set(const juce::String& label, int value, int min, int max,
           int step = 1, const juce::String& hint = {});
  void setValue(int);
  bool isEditing() const;
  void resized() override;

 private:
  void textEditorReturnKeyPressed(juce::TextEditor&) override;
  void textEditorFocusLost(juce::TextEditor&) override;
  void commit();

  juce::Label label_;
  juce::TextEditor edit_;
  juce::Label hint_;
  int min_ = 0;
  int max_ = 0;
  int last_ = 0;
};

class TextField : public juce::Component, private juce::TextEditor::Listener {
 public:
  std::function<void(juce::String)> onChange;

  TextField();
  void set(const juce::String& label, const juce::String& value, int maxLen,
           const juce::String& hint = {}, bool password = false,
           const juce::String& placeholder = {});
  void setValue(const juce::String&);
  juce::String value() const { return edit_.getText(); }
  bool isEditing() const;
  void resized() override;

 private:
  void textEditorTextChanged(juce::TextEditor&) override;
  void textEditorFocusLost(juce::TextEditor&) override;

  juce::Label label_;
  juce::TextEditor edit_;
  juce::Label hint_;
  bool suppress_ = false;
};

class SelectField : public juce::Component {
 public:
  std::function<void(int)> onChange;

  SelectField();
  ~SelectField() override;
  void set(const juce::String& label, int selectedId,
           const std::vector<Option>& options,
           const juce::String& hint = {});
  void setSelected(int id);
  void resized() override;
  void parentHierarchyChanged() override;

 private:
  void toggleList();
  void hideList();
  void choose(int id);
  void layoutList();
  bool listOpen() const { return list_ != nullptr; }

  class ListOverlay;

  juce::Label label_;
  juce::TextButton btn_{"—"};
  juce::Label hint_;
  std::vector<Option> options_;
  int selected_ = 0;
  std::unique_ptr<ListOverlay> list_;
};

class Readout : public juce::Component {
 public:
  Readout();
  void set(const juce::String& label, const juce::String& value);
  void resized() override;

 private:
  juce::Label label_;
  juce::Label value_;
};

class SubNav : public juce::Component {
 public:
  std::function<void(int)> onChange;

  void setItems(const std::vector<juce::String>& items, int selected);
  int selected() const { return selected_; }
  void resized() override;

 private:
  juce::OwnedArray<juce::TextButton> btns_;
  int selected_ = 0;
};

class TabBar : public juce::Component {
 public:
  std::function<void(int)> onChange;

  void setTabs(const std::vector<juce::String>& tabs, int selected);
  int selected() const { return selected_; }
  void resized() override;
  void paint(juce::Graphics&) override;

 private:
  juce::OwnedArray<juce::TextButton> btns_;
  int selected_ = 0;
};

class StepGrid : public juce::Component {
 public:
  std::function<void(int /*step*/)> onToggle;

  void set(int steps, uint64_t mask);
  void resized() override;

 private:
  juce::OwnedArray<juce::TextButton> cells_;
  int steps_ = 0;
  uint64_t mask_ = 0;
};

class Meter : public juce::Component {
 public:
  void set(const juce::String& label, int milli);  // 0..1000
  void paint(juce::Graphics&) override;

 private:
  juce::String label_;
  int milli_ = 0;
};

class Confirm : public juce::Component {
 public:
  std::function<void()> onConfirm;
  std::function<void()> onCancel;

  Confirm();
  void set(const juce::String& title, const juce::String& body,
           const juce::String& confirm);
  void resized() override;
  void paint(juce::Graphics&) override;

 private:
  juce::Label title_;
  juce::Label body_;
  juce::TextButton ok_;
  juce::TextButton cancel_{"Cancel"};
};

// Vertical packer. Call next(h) for each row.
struct Stack {
  juce::Rectangle<int> r;
  int gap = 8;
  juce::Rectangle<int> next(int h) {
    auto out = r.removeFromTop(h);
    r.removeFromTop(gap);
    return out;
  }
  void skip(int h) {
    r.removeFromTop(h);
    r.removeFromTop(gap);
  }
};

}  // namespace neon::ui
