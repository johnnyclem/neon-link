#!/usr/bin/env python3
"""Assert the palette actually meets its contrast targets.

DESIGN_SYSTEM.md §12 makes high contrast non-negotiable on both surfaces: the
module has to stay readable at arm's length in a dark modular case, and the web
UI gets used on a phone in a dim studio. Palettes drift when they are only
checked by eye, so the ratios are asserted here and in CI against the pairs
declared in design/tokens.json.

    python3 scripts/check_contrast.py

Standard library only. Exits non-zero if any declared pair falls short.
"""

from __future__ import annotations

import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
TOKENS = ROOT / "design" / "tokens.json"


def srgb_to_linear(channel: float) -> float:
    return channel / 12.92 if channel <= 0.04045 else ((channel + 0.055) / 1.055) ** 2.4


def relative_luminance(hex_color: str) -> float:
    value = hex_color.lstrip("#")
    if len(value) != 6:
        raise SystemExit(f"expected a 6-digit hex colour, got '{hex_color}'")
    r, g, b = (int(value[i : i + 2], 16) / 255.0 for i in (0, 2, 4))
    return (
        0.2126 * srgb_to_linear(r)
        + 0.7152 * srgb_to_linear(g)
        + 0.0722 * srgb_to_linear(b)
    )


def contrast_ratio(fg: str, bg: str) -> float:
    a, b = relative_luminance(fg), relative_luminance(bg)
    lighter, darker = max(a, b), min(a, b)
    return (lighter + 0.05) / (darker + 0.05)


THEME_SKIP = {"$comment", "default"}


def color_hexes(block: dict) -> dict[str, str]:
    out: dict[str, str] = {}
    for name, entry in block.items():
        if name.startswith("$"):
            continue
        out[name] = entry["hex"] if isinstance(entry, dict) else entry
    return out


def check_palette(name: str, colors: dict[str, str], pairs: list[dict]) -> int:
    failures = 0
    print(f"\n{name}")
    print(f"{'pair':<28} {'ratio':>7}  {'min':>5}  result")
    print("-" * 60)
    for pair in pairs:
        fg_name, bg_name, minimum = pair["fg"], pair["bg"], pair["min"]
        for token in (fg_name, bg_name):
            if token not in colors:
                raise SystemExit(f"{name}: contrast pair references unknown colour '{token}'")
        ratio = contrast_ratio(colors[fg_name], colors[bg_name])
        ok = ratio >= minimum
        if not ok:
            failures += 1
        label = f"{fg_name} on {bg_name}"
        print(f"{label:<28} {ratio:>6.2f}:1 {minimum:>5.1f}  {'ok' if ok else 'FAIL'}   {pair.get('note', '')}")
    return failures


def main() -> int:
    with TOKENS.open(encoding="utf-8") as fh:
        tokens = json.load(fh)

    default = color_hexes(tokens["color"])
    pairs = tokens["contrast"]["pairs"]
    failures = check_palette("default (void)", default, pairs)

    themes = tokens.get("themes") or {}
    for tid, theme in themes.items():
        if tid in THEME_SKIP:
            continue
        colors = dict(default)
        if "color" in theme:
            colors.update(color_hexes(theme["color"]))
        failures += check_palette(tid, colors, pairs)

    print()
    if failures:
        print(f"{failures} contrast pair(s) below target.", file=sys.stderr)
        return 1
    print("All palettes meet their contrast targets.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
