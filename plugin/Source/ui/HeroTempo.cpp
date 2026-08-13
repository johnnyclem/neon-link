#include "HeroTempo.h"
#include "Tokens.h"

#include <cmath>

namespace neon::ui {
namespace {

struct Seg {
  float x, y, w, h;
};
// design/fonts/hero.json, cell 16×26, stroke 4.
constexpr Seg kA{0, 0, 16, 4};
constexpr Seg kB{12, 0, 4, 15};
constexpr Seg kC{12, 11, 4, 15};
constexpr Seg kD{0, 22, 16, 4};
constexpr Seg kE{0, 11, 4, 15};
constexpr Seg kF{0, 0, 4, 15};
constexpr Seg kG{0, 11, 16, 4};
constexpr int kSegN = 7;
constexpr Seg kSegs[kSegN] = {kA, kB, kC, kD, kE, kF, kG};

// Bits A..G = 0..6
constexpr uint8_t kGlyph[] = {
    /*0*/ 0x3f, /*1*/ 0x06, /*2*/ 0x5b, /*3*/ 0x4f, /*4*/ 0x66,
    /*5*/ 0x6d, /*6*/ 0x7d, /*7*/ 0x07, /*8*/ 0x7f, /*9*/ 0x6f,
};

void drawSeg(juce::Graphics& g, float ox, float oy, float s, const Seg& r,
             bool on) {
  g.setColour(on ? neon() : surface2());
  g.fillRect(ox + r.x * s, oy + r.y * s, r.w * s, r.h * s);
}

void drawDigit(juce::Graphics& g, float ox, float oy, float s, char ch) {
  if (ch == '.') {
    g.setColour(neon());
    g.fillRect(ox + 1 * s, oy + 22 * s, 4 * s, 4 * s);
    return;
  }
  if (ch == '-') {
    drawSeg(g, ox, oy, s, kG, true);
    for (int i = 0; i < kSegN; ++i) {
      if (i != 6) drawSeg(g, ox, oy, s, kSegs[i], false);
    }
    return;
  }
  if (ch < '0' || ch > '9') return;
  const uint8_t bits = kGlyph[ch - '0'];
  for (int i = 0; i < kSegN; ++i) {
    drawSeg(g, ox, oy, s, kSegs[i], (bits & (1u << i)) != 0);
  }
}

}  // namespace

void HeroTempo::setBpm(double bpm, bool valid) {
  if (valid_ == valid && std::abs(bpm_ - bpm) < 0.05) return;
  bpm_ = bpm;
  valid_ = valid;
  repaint();
}

void HeroTempo::paint(juce::Graphics& g) {
  const juce::String text =
      valid_ ? juce::String(bpm_, 1) : juce::String("--.-");
  const float cellH = 26.0f;
  const float s = static_cast<float>(getHeight()) / cellH;
  const float cellW = 16.0f;
  const float track = 2.0f;
  float x = 0;
  for (int i = 0; i < text.length(); ++i) {
    const juce::juce_wchar ch = text[i];
    if (i > 0) x += track * s;
    const float gw = (ch == '.' ? 6.0f : cellW) * s;
    drawDigit(g, x, 0, s, static_cast<char>(ch));
    x += gw;
  }
  g.setColour(muted());
  g.setFont(juce::Font(juce::FontOptions(11.0f)));
  g.drawText("BPM", juce::Rectangle<int>(static_cast<int>(x) + 8, 0, 40,
                                         getHeight()),
             juce::Justification::centredLeft, false);
}

}  // namespace neon::ui
