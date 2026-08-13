#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// DESIGN_SYSTEM.md palette. Hard-coded so the editor compiles without
// gen_design.py; PR 7 later can emit these from design/tokens.json.
namespace neon::ui {

inline juce::Colour bg() { return juce::Colour(0xff0b0c0f); }
inline juce::Colour surface() { return juce::Colour(0xff14161a); }
inline juce::Colour surface2() { return juce::Colour(0xff1c1f26); }
inline juce::Colour border() { return juce::Colour(0xff2a2e38); }
inline juce::Colour text() { return juce::Colour(0xffe8eaed); }
inline juce::Colour muted() { return juce::Colour(0xff8b909a); }
inline juce::Colour neon() { return juce::Colour(0xff00f0ff); }
inline juce::Colour neonDim() { return juce::Colour(0xff00a8b3); }
inline juce::Colour magenta() { return juce::Colour(0xffff2d95); }
inline juce::Colour yellow() { return juce::Colour(0xfff5c518); }
inline juce::Colour success() { return juce::Colour(0xff3dff9a); }
inline juce::Colour danger() { return juce::Colour(0xffff4d4d); }

constexpr int kBarW = 128;
constexpr int kBarH = 16;
constexpr int kBarInset = 3;
constexpr int kBarTickH = 3;

}  // namespace neon::ui
