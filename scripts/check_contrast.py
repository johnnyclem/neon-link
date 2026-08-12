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


def main() -> int:
    with TOKENS.open(encoding="utf-8") as fh:
        tokens = json.load(fh)

    colors = {
        name: entry["hex"]
        for name, entry in tokens["color"].items()
        if not name.startswith("$")
    }
    pairs = tokens["contrast"]["pairs"]

    failures = 0
    print(f"{'pair':<28} {'ratio':>7}  {'min':>5}  result")
    print("-" * 60)
    for pair in pairs:
        fg_name, bg_name, minimum = pair["fg"], pair["bg"], pair["min"]
        for name in (fg_name, bg_name):
            if name not in colors:
                raise SystemExit(f"contrast pair references unknown colour '{name}'")
        ratio = contrast_ratio(colors[fg_name], colors[bg_name])
        ok = ratio >= minimum
        if not ok:
            failures += 1
        label = f"{fg_name} on {bg_name}"
        print(f"{label:<28} {ratio:>6.2f}:1 {minimum:>5.1f}  {'ok' if ok else 'FAIL'}   {pair.get('note', '')}")

    print()
    if failures:
        print(f"{failures} contrast pair(s) below target.", file=sys.stderr)
        return 1
    print(f"All {len(pairs)} contrast pairs meet their targets.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
