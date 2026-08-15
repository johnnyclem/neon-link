// GENERATED FILE - do not edit.
// Source: design/tokens.json, design/strings.json, design/icons.txt, design/fonts/hero.json
// Regenerate: python3 scripts/gen_design.py

export const tokens = {
  "color": {
    "bg": "#0B0C0F",
    "surface": "#14161A",
    "surface-2": "#1C1F26",
    "border": "#2A2E38",
    "text": "#E8EAED",
    "text-muted": "#8B909A",
    "neon": "#00F0FF",
    "neon-dim": "#00A8B3",
    "magenta": "#FF2D95",
    "yellow": "#F5C518",
    "success": "#3DFF9A",
    "danger": "#FF4D4D"
  },
  "colorUse": {
    "bg": "Page background",
    "surface": "Cards, panels",
    "surface-2": "Elevated / selected",
    "border": "Hairlines, dividers",
    "text": "Primary text",
    "text-muted": "Secondary labels",
    "neon": "Primary accent \u2014 Link active, focus, key actions",
    "neon-dim": "Secondary accent / hover",
    "magenta": "Alerts, BLE active, special states",
    "yellow": "Warnings, attention",
    "success": "Connected / running",
    "danger": "Errors, disconnect"
  },
  "space": {
    "0": 0,
    "1": 4,
    "2": 8,
    "3": 12,
    "4": 16,
    "5": 24,
    "6": 32,
    "7": 48,
    "8": 64
  },
  "type": {
    "size": {
      "hero": 64,
      "title": 24,
      "subtitle": 18,
      "body": 15,
      "label": 13,
      "caption": 11
    },
    "weight": {
      "regular": 400,
      "medium": 500,
      "bold": 700
    }
  },
  "motion": {
    "fast": 150,
    "normal": 200,
    "easing": "cubic-bezier(0.2, 0, 0, 1)"
  },
  "device": {
    "width": 128,
    "height": 128,
    "margin": 4,
    "header_text_y": 2,
    "header_rule_y": 12,
    "hero_y": 22,
    "unit_y": 52,
    "status_y": 66,
    "ident_y": 78,
    "bar_y": 104,
    "bar_h": 16,
    "bar_inset": 3,
    "bar_tick_h": 3,
    "list_top": 16,
    "list_row_h": 12,
    "list_rows": 9,
    "list_gutter": 10,
    "icon_size": 8,
    "focus_inset": 1,
    "confirm_line1_y": 40,
    "confirm_line2_y": 52,
    "confirm_box_y": 84,
    "confirm_box_h": 18
  },
  "deviceCompact": {
    "width": 128,
    "height": 64,
    "hero_y": 15,
    "unit_y": -1,
    "status_y": 44,
    "ident_y": -1,
    "bar_y": 54,
    "bar_h": 8,
    "bar_tick_h": 2,
    "list_top": 16,
    "list_row_h": 12,
    "list_rows": 4,
    "confirm_line1_y": 17,
    "confirm_line2_y": 27,
    "confirm_box_y": 42,
    "confirm_box_h": 16
  },
  "phaseBar": {
    "aspect": 8.0,
    "inset_ratio": 0.1875,
    "tick_ratio": 0.1875
  },
  "touchMin": 44,
  "contrastPairs": [
    {
      "fg": "text",
      "bg": "bg",
      "min": 4.5,
      "note": "body on page"
    },
    {
      "fg": "text",
      "bg": "surface",
      "min": 4.5,
      "note": "body on card"
    },
    {
      "fg": "text",
      "bg": "surface-2",
      "min": 4.5,
      "note": "body on elevated"
    },
    {
      "fg": "text-muted",
      "bg": "bg",
      "min": 4.5,
      "note": "secondary labels"
    },
    {
      "fg": "text-muted",
      "bg": "surface",
      "min": 4.5,
      "note": "secondary on card"
    },
    {
      "fg": "neon",
      "bg": "bg",
      "min": 4.5,
      "note": "accent text and focus ring"
    },
    {
      "fg": "neon",
      "bg": "surface",
      "min": 4.5,
      "note": "accent on card"
    },
    {
      "fg": "neon-dim",
      "bg": "bg",
      "min": 3.0,
      "note": "hover/secondary accent, UI boundary"
    },
    {
      "fg": "magenta",
      "bg": "bg",
      "min": 3.0,
      "note": "BLE state chip, large/bold only"
    },
    {
      "fg": "magenta",
      "bg": "surface",
      "min": 3.0,
      "note": "BLE state on card"
    },
    {
      "fg": "yellow",
      "bg": "bg",
      "min": 4.5,
      "note": "warning text"
    },
    {
      "fg": "success",
      "bg": "bg",
      "min": 4.5,
      "note": "connected/running text"
    },
    {
      "fg": "danger",
      "bg": "bg",
      "min": 4.5,
      "note": "error text"
    },
    {
      "fg": "danger",
      "bg": "surface",
      "min": 4.5,
      "note": "error on card"
    },
    {
      "fg": "border",
      "bg": "bg",
      "min": 1.3,
      "note": "hairline only \u2014 must be visible, not loud"
    }
  ]
} as const;

export type ColorName = keyof typeof tokens.color;
