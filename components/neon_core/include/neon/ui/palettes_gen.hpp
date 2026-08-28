// GENERATED FILE - do not edit.
// Source: design/tokens.json, design/strings.json, design/icons.txt, design/fonts/hero.json
// Regenerate: python3 scripts/gen_design.py


#pragma once

#include <cstdint>

namespace neon::ui {

struct Rgb {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

struct ColorPalette {
  Rgb bg;
  Rgb surface;
  Rgb surface2;
  Rgb neon;
  Rgb neon_dim;
  Rgb text;
  Rgb text_muted;
  Rgb magenta;
  Rgb hero;
};

inline constexpr int kColorThemeCount = 6;

inline constexpr const char* kColorThemeId[kColorThemeCount] = {"void", "teal", "phosphor", "amber", "magenta", "paper"};

inline constexpr const char* kColorThemeLabel[kColorThemeCount] = {"VOID", "TEAL", "PHOSPHOR", "AMBER", "MAGENTA", "PAPER"};

inline constexpr ColorPalette kColorPalettes[kColorThemeCount] = {
    {{11, 12, 15}, {20, 22, 26}, {28, 31, 38}, {0, 240, 255}, {0, 168, 179}, {232, 234, 237}, {139, 144, 154}, {255, 45, 149}, {0, 240, 255}},  // void
    {{10, 26, 30}, {18, 38, 44}, {26, 52, 60}, {94, 212, 220}, {58, 160, 168}, {214, 230, 234}, {138, 173, 180}, {255, 90, 168}, {126, 224, 230}},  // teal
    {{7, 17, 10}, {14, 28, 18}, {22, 40, 26}, {61, 255, 154}, {34, 184, 106}, {212, 232, 214}, {138, 170, 144}, {255, 77, 166}, {108, 255, 176}},  // phosphor
    {{20, 16, 10}, {30, 24, 16}, {42, 34, 22}, {255, 176, 32}, {196, 132, 24}, {242, 230, 208}, {176, 154, 116}, {255, 77, 138}, {255, 194, 74}},  // amber
    {{20, 10, 18}, {30, 16, 24}, {42, 24, 36}, {255, 90, 176}, {208, 64, 144}, {240, 224, 234}, {176, 144, 160}, {255, 45, 149}, {255, 122, 196}},  // magenta
    {{243, 238, 230}, {255, 251, 245}, {230, 223, 212}, {0, 114, 120}, {10, 138, 148}, {26, 24, 20}, {92, 86, 76}, {196, 0, 106}, {0, 100, 108}},  // paper
};

inline const ColorPalette& color_palette(uint8_t theme) {
  if (theme >= kColorThemeCount) {
    theme = 0;
  }
  return kColorPalettes[theme];
}

}  // namespace neon::ui
