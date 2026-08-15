// GENERATED FILE - do not edit.
// Source: design/tokens.json, design/strings.json, design/icons.txt, design/fonts/hero.json
// Regenerate: python3 scripts/gen_design.py


#pragma once

#include <cstdint>

namespace neon::ui {

// 128x128 panel geometry. Every screen positions itself from these -
// no screen may hard-code a pixel offset.
inline constexpr int kWidth = 128;
inline constexpr int kHeight = 128;
inline constexpr int kMargin = 4;
inline constexpr int kHeaderTextY = 2;
inline constexpr int kHeaderRuleY = 12;
inline constexpr int kHeroY = 22;
inline constexpr int kUnitY = 52;
inline constexpr int kStatusY = 66;
inline constexpr int kIdentY = 78;
inline constexpr int kBarY = 104;
inline constexpr int kBarH = 16;
inline constexpr int kBarInset = 3;
inline constexpr int kBarTickH = 3;
inline constexpr int kListTop = 16;
inline constexpr int kListRowH = 12;
inline constexpr int kListRows = 9;
inline constexpr int kListGutter = 10;
inline constexpr int kIconSize = 8;
inline constexpr int kFocusInset = 1;
inline constexpr int kConfirmLine1Y = 40;
inline constexpr int kConfirmLine2Y = 52;
inline constexpr int kConfirmBoxY = 84;
inline constexpr int kConfirmBoxH = 18;

// The vertical flow, as a value: the rows that differ between the
// full 128x128 layout and the compact 128x64 one (native
// SSD1306/1309 panels). render_ui() takes one of the two instances
// below; -1 marks a band the compact flow drops (unit label,
// identity row). Shared chrome stays in the constants above.
struct Layout {
  int height;
  int hero_y;
  int unit_y;
  int status_y;
  int ident_y;
  int bar_y;
  int bar_h;
  int bar_tick_h;
  int list_top;
  int list_row_h;
  int list_rows;
  int confirm_line1_y;
  int confirm_line2_y;
  int confirm_box_y;
  int confirm_box_h;
};
inline constexpr Layout kLayout128 = {128, 22, 52, 66, 78, 104, 16, 3, 16, 12, 9, 40, 52, 84, 18};
inline constexpr Layout kLayout64 = {64, 15, -1, 44, -1, 54, 8, 2, 16, 12, 4, 17, 27, 42, 16};

// Shared status vocabulary. The web shows the long form of the same
// entry, so the two surfaces never invent synonyms for one state.
inline constexpr const char kWordSourceLink[] = "LINK";
inline constexpr const char kWordSourceExt[] = "EXT";
inline constexpr const char kWordTransportRun[] = "RUN";
inline constexpr const char kWordTransportStop[] = "STOP";
inline constexpr const char kWordNetAp[] = "AP";
inline constexpr const char kWordNetWifi[] = "STA";
inline constexpr const char kWordNetEthernet[] = "ETH";
inline constexpr const char kWordNetNone[] = "OFF";
inline constexpr const char kWordBleOn[] = "BLE";
inline constexpr const char kWordBleOff[] = "";
inline constexpr const char kWordNoLink[] = "NO LINK";
inline constexpr const char kWordSaved[] = "SAVED";

inline constexpr const char kTitleLive[] = "LIVE";
inline constexpr const char kTitleOutputs[] = "OUTPUTS";
inline constexpr const char kTitleNetwork[] = "NETWORK";
inline constexpr const char kTitleMidi[] = "MIDI";
inline constexpr const char kTitleAudio[] = "AUDIO";
inline constexpr const char kTitleSystem[] = "SYSTEM";

inline constexpr const char kBrand[] = "NEON";
inline constexpr const char kSetupIp[] = "192.168.4.1";

}  // namespace neon::ui
