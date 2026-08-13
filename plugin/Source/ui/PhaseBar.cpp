#include "PhaseBar.h"
#include "Tokens.h"

namespace neon::ui {

void PhaseBar::setPhase(float phase01, int quantum, bool running) {
  phase_ = juce::jlimit(0.0f, 1.0f, phase01);
  quantum_ = quantum > 0 ? quantum : 4;
  running_ = running;
  repaint();
}

void PhaseBar::paint(juce::Graphics& g) {
  const float viewH = static_cast<float>(kBarH + 2 * kBarTickH);
  const float scale = static_cast<float>(getHeight()) / viewH;
  const float w = static_cast<float>(kBarW) * scale;
  const float x0 = (static_cast<float>(getWidth()) - w) * 0.5f;
  const float y0 = static_cast<float>(kBarTickH) * scale;
  const float barH = static_cast<float>(kBarH) * scale;
  const float inset = static_cast<float>(kBarInset) * scale;

  g.setColour(muted());
  g.drawRect(x0, y0, w, barH, 1.0f);

  const float track = w - 2.0f * inset;
  const float fill = track * phase_;
  if (fill > 0.5f) {
    const juce::Rectangle<float> r(x0 + inset, y0 + inset, fill,
                                   barH - 2.0f * inset);
    if (running_) {
      g.setColour(neon());
      g.fillRect(r);
    } else {
      // Held fill: 50% dither, same idea as the panel.
      g.setColour(neon());
      const int x1 = static_cast<int>(r.getRight());
      const int y1 = static_cast<int>(r.getBottom());
      for (int y = static_cast<int>(r.getY()); y < y1; ++y) {
        for (int x = static_cast<int>(r.getX()); x < x1; ++x) {
          if (((x + y) & 1) == 0) g.fillRect(x, y, 1, 1);
        }
      }
    }
  }

  g.setColour(muted());
  const float tick = static_cast<float>(kBarTickH) * scale;
  for (int i = 1; i < quantum_; ++i) {
    const float x = x0 + w * static_cast<float>(i) / static_cast<float>(quantum_);
    g.drawLine(x, y0 - tick, x, y0, 1.0f);
    g.drawLine(x, y0 + barH, x, y0 + barH + tick, 1.0f);
  }
}

}  // namespace neon::ui
