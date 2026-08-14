#!/usr/bin/env python3
"""Generate both UI surfaces from design/.

The web configuration interface and the 128x128 device display are supposed to
feel like two surfaces of the same object. The only way to guarantee that over
time is to stop maintaining the two independently: colours, status vocabulary,
icons, layout proportions and the hero numerals all live in design/, and this
script emits the CSS, the TypeScript and the C++ headers from there.

    python3 scripts/gen_design.py [--check]

--check verifies the generated files on disk match what would be emitted and
exits non-zero otherwise (this is what CI runs). Standard library only: the
firmware build must never depend on a Python environment.
"""

from __future__ import annotations

import argparse
import binascii
import json
import pathlib
import struct
import sys
import zlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
DESIGN = ROOT / "design"

BANNER_SOURCES = "design/tokens.json, design/strings.json, design/icons.txt, design/fonts/hero.json"

# What advances an icon's frame. Kept here rather than inferred, so a typo in
# icons.txt fails the build instead of silently freezing an animation.
#   beat   one frame per musical beat, from the shared phase
#   tick   a fixed-rate counter, for motion with no musical time
#   static single frame
ICON_CLOCKS = {"static", "beat", "tick"}

# The fixed-rate clock, in frames per second. Slow enough that a 3-6 frame
# loop lasts most of a second, which is the difference between an icon that
# reads as pulsing and one that reads as flickering.
ICON_TICK_HZ = 5


def banner(comment_open: str, comment_close: str = "") -> str:
    lines = [
        "GENERATED FILE - do not edit.",
        f"Source: {BANNER_SOURCES}",
        "Regenerate: python3 scripts/gen_design.py",
    ]
    body = "\n".join(f" * {line}" if comment_open == "/*" else f"{comment_open} {line}" for line in lines)
    if comment_open == "/*":
        return "/*\n" + body + "\n */\n"
    return body + "\n"


# ---------------------------------------------------------------------------
# loading
# ---------------------------------------------------------------------------


def load_json(path: pathlib.Path) -> dict:
    with path.open(encoding="utf-8") as fh:
        return json.load(fh)


def strip_meta(obj: dict) -> dict:
    """Drop the $comment / $note keys used to document the source files."""
    return {k: v for k, v in obj.items() if not k.startswith("$")}


def load_icons(path: pathlib.Path) -> "dict[str, list[str]]":
    icons: dict[str, list[str]] = {}
    name: str | None = None
    clock: str = "static"
    rows: list[str] = []

    def flush() -> None:
        if name is None:
            return
        if not rows or len(rows) % 8 != 0:
            raise SystemExit(
                f"icon '{name}': expected whole 8-row frames, got {len(rows)} rows"
            )
        for row in rows:
            if len(row) != 8:
                raise SystemExit(f"icon '{name}': row '{row}' is {len(row)} chars, expected 8")
            if set(row) - {"#", "."}:
                raise SystemExit(f"icon '{name}': row '{row}' has characters other than # and .")
        frames = [rows[i : i + 8] for i in range(0, len(rows), 8)]
        if clock != "static" and len(frames) < 2:
            raise SystemExit(
                f"icon '{name}': clock '{clock}' declared but only one frame"
            )
        if clock == "static" and len(frames) > 1:
            raise SystemExit(
                f"icon '{name}': {len(frames)} frames but no clock — add 'beat' or 'tick'"
            )
        icons[name] = {"clock": clock, "frames": frames}

    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.rstrip()
        if line.startswith(";"):
            continue
        if line.startswith("@"):
            flush()
            parts = line[1:].split()
            name = parts[0]
            clock = parts[1] if len(parts) > 1 else "static"
            if clock not in ICON_CLOCKS:
                raise SystemExit(
                    f"icon '{name}': unknown clock '{clock}' "
                    f"(expected one of {', '.join(sorted(ICON_CLOCKS))})"
                )
            rows = []
            continue
        if not line.strip():
            continue
        rows.append(line)
    flush()
    if not icons:
        raise SystemExit(f"{path}: no icons found")
    return icons


# ---------------------------------------------------------------------------
# hero font rasterisation
# ---------------------------------------------------------------------------


def raster_hero(spec: dict) -> "dict[str, tuple[int, list[int]]]":
    """Rasterise the segment map into (advance width, column bitmasks).

    Each column is a bitmask where bit N is row N counting down from the top,
    which is the orientation the firmware blits in.
    """
    cell_w = spec["cell"]["width"]
    cell_h = spec["cell"]["height"]
    segments = strip_meta(spec["segments"])
    if cell_h > 32:
        raise SystemExit("hero cell height must fit a uint32 column")

    out: dict[str, tuple[int, list[int]]] = {}
    for ch, glyph in spec["glyphs"].items():
        width = glyph.get("width", cell_w)
        rects = list(glyph.get("rects", []))
        for seg_name in glyph.get("on", []):
            if seg_name not in segments:
                raise SystemExit(f"glyph '{ch}': unknown segment '{seg_name}'")
            rects.append(segments[seg_name])

        cols = [0] * width
        for rect in rects:
            for x in range(rect["x"], rect["x"] + rect["w"]):
                for y in range(rect["y"], rect["y"] + rect["h"]):
                    if not (0 <= x < width and 0 <= y < cell_h):
                        raise SystemExit(f"glyph '{ch}': rect escapes the {width}x{cell_h} cell")
                    cols[x] |= 1 << y
        out[ch] = (width, cols)
    return out


def hero_preview(spec: dict, raster: "dict[str, tuple[int, list[int]]]") -> str:
    cell_h = spec["cell"]["height"]
    lines = [
        "NEON LINK hero numerals - GENERATED PREVIEW, do not edit.",
        "Source: design/fonts/hero.json   Regenerate: python3 scripts/gen_design.py",
        "",
        "This file exists so a change to the segment geometry shows up as readable",
        "pixels in the diff instead of as a wall of hex.",
        "",
    ]
    for ch, (width, cols) in raster.items():
        label = "space" if ch == " " else ch
        lines.append(f"'{label}'  {width}x{cell_h}")
        for y in range(cell_h):
            lines.append("".join("#" if (cols[x] >> y) & 1 else "." for x in range(width)))
        lines.append("")
    return "\n".join(lines) + "\n"


# ---------------------------------------------------------------------------
# emitters
# ---------------------------------------------------------------------------


def emit_tokens_css(tokens: dict) -> str:
    colors = strip_meta(tokens["color"])
    space = strip_meta(tokens["space"])
    typ = tokens["type"]
    motion = strip_meta(tokens["motion"])
    device = strip_meta(tokens["device"])

    out = [banner("/*"), ":root {"]
    out.append("  /* colour */")
    for name, entry in colors.items():
        out.append(f"  --{name}: {entry['hex']}; /* {entry['use']} */")

    out.append("")
    out.append("  /* spacing - 4px grid, shared with the device layout */")
    for name, value in space.items():
        out.append(f"  --space-{name}: {value}px;")

    out.append("")
    out.append("  /* geometry */")
    out.append(f"  --radius: {tokens['radius']['none']}px;")
    out.append(f"  --border-hair: {tokens['border']['hair']}px;")
    out.append(f"  --border-solid: {tokens['border']['solid']}px;")
    out.append(f"  --touch-min: {tokens['touch']['min']}px;")

    out.append("")
    out.append("  /* type */")
    out.append(f"  --font-ui: {typ['family']['ui']};")
    out.append(f"  --font-mono: {typ['family']['mono']};")
    for name, value in typ["size"].items():
        out.append(f"  --text-{name}: {value}px;")
    for name, value in typ["weight"].items():
        out.append(f"  --weight-{name}: {value};")
    for name, value in strip_meta(typ["tracking"]).items():
        out.append(f"  --tracking-{name}: {value};")
    for name, value in typ["leading"].items():
        out.append(f"  --leading-{name}: {value};")

    out.append("")
    out.append("  /* motion */")
    out.append(f"  --motion-fast: {motion['fast']}ms;")
    out.append(f"  --motion-normal: {motion['normal']}ms;")
    out.append(f"  --motion-easing: {motion['easing']};")

    out.append("")
    out.append("  /* device geometry - lets the web draw the panel's real proportions */")
    for name, value in device.items():
        if isinstance(value, int):
            out.append(f"  --device-{name.replace('_', '-')}: {value};")
    out.append(f"  --phase-bar-aspect: {tokens['phase_bar']['aspect']};")
    out.append(f"  --phase-bar-inset-ratio: {tokens['phase_bar']['inset_ratio']};")
    out.append(f"  --phase-bar-tick-ratio: {tokens['phase_bar']['tick_ratio']};")
    out.append("}")
    return "\n".join(out) + "\n"


def emit_tokens_ts(tokens: dict) -> str:
    colors = {name: entry["hex"] for name, entry in strip_meta(tokens["color"]).items()}
    payload = {
        "color": colors,
        "colorUse": {name: entry["use"] for name, entry in strip_meta(tokens["color"]).items()},
        "space": strip_meta(tokens["space"]),
        "type": {
            "size": tokens["type"]["size"],
            "weight": tokens["type"]["weight"],
        },
        "motion": strip_meta(tokens["motion"]),
        "device": strip_meta(tokens["device"]),
        "phaseBar": strip_meta(tokens["phase_bar"]),
        "touchMin": tokens["touch"]["min"],
        "contrastPairs": tokens["contrast"]["pairs"],
    }
    body = json.dumps(payload, indent=2)
    return (
        banner("//")
        + "\n"
        + f"export const tokens = {body} as const;\n\n"
        + "export type ColorName = keyof typeof tokens.color;\n"
    )


def emit_strings_ts(strings: dict) -> str:
    payload = {
        "vocabulary": strip_meta(strings["vocabulary"]),
        "states": strip_meta(strings["states"]),
        "screens": strip_meta(strings["screens"]),
        "brand": strings["brand"],
    }
    body = json.dumps(payload, indent=2)
    return (
        banner("//")
        + "\n"
        + f"export const strings = {body} as const;\n\n"
        + "export type StateName = keyof typeof strings.states;\n"
        + "export type ScreenName = keyof typeof strings.screens;\n\n"
        + "/** The device's word for a state - use this wherever the panel would show it. */\n"
        + "export const deviceWord = (name: StateName): string => strings.states[name].device;\n"
    )


def emit_icons_ts(icons: dict) -> str:
    body = json.dumps(icons, indent=2)
    return (
        banner("//")
        + "\n"
        + "/*\n"
        + " * The 8x8 1-bit masters, verbatim, with their frames and the clock\n"
        + " * that advances them. The web renders these as crisp SVG pixel grids\n"
        + " * rather than redrawing them as vectors, so the icon on the page is\n"
        + " * pixel-for-pixel the icon on the panel - and plays the same loop.\n"
        + " */\n"
        + f"export const iconMasters = {body} as const;\n\n"
        + "export type IconName = keyof typeof iconMasters;\n\n"
        + "export const ICON_SIZE = 8;\n\n"
        + "/** Frames per second for the fixed-rate clock. */\n"
        + f"export const ICON_TICK_HZ = {ICON_TICK_HZ};\n"
    )


def emit_hero_font_ts(spec: dict) -> str:
    """The segment geometry, so the web can draw the panel's own numerals.

    The web hero tempo is not a font that resembles the device's - it is the
    same segment map, rendered as SVG. The browser can additionally show the
    unlit segments as ghosts, which a 1-bit panel cannot.
    """
    payload = {
        "cell": spec["cell"],
        "tracking": spec["tracking"],
        "segments": strip_meta(spec["segments"]),
        "glyphs": {
            ch: {
                "width": glyph.get("width", spec["cell"]["width"]),
                "on": glyph.get("on", []),
                "rects": glyph.get("rects", []),
            }
            for ch, glyph in spec["glyphs"].items()
        },
    }
    body = json.dumps(payload, indent=2)
    return (
        banner("//")
        + "\n"
        + f"export const heroFont = {body} as const;\n\n"
        + "export type SegmentName = keyof typeof heroFont.segments;\n"
    )


def emit_icons_preview_png(tokens: dict, icons: dict) -> bytes:
    """A contact sheet: every icon, every frame, at the sizes it ships at.

    Two icons shipped unreadable before this existed, and both times the
    reason was the same: they were only ever looked at blown up. An 8x8
    master reviewed at 32 px tells you nothing about whether it survives in
    the panel header. So every frame is drawn at 8x, 2x and 1x stacked, and
    the small copies are the ones that decide whether it works.

    One row per icon, in design/icons.txt order, so the two files read side
    by side; one column per frame, left to right in playback order. Palette
    comes from tokens.json, so the sheet always shows an icon on the ground
    it is actually drawn on.
    """
    scales = [8, 2, 1]
    pad, col_gap, row_gap, stack_gap = 12, 10, 14, 4
    frame_w = 8 * max(scales)
    cell_h = sum(8 * s for s in scales) + stack_gap * (len(scales) - 1)
    max_frames = max(len(icon["frames"]) for icon in icons.values())

    width = pad * 2 + max_frames * frame_w + col_gap * (max_frames - 1)
    height = pad * 2 + len(icons) * (cell_h + row_gap) - row_gap

    def rgb(hex_color: str) -> "tuple[int, int, int]":
        v = hex_color.lstrip("#")
        return (int(v[0:2], 16), int(v[2:4], 16), int(v[4:6], 16))

    colors = strip_meta(tokens["color"])
    bg = rgb(colors["bg"]["hex"])
    fg = rgb(colors["neon"]["hex"])
    # Animated rows are marked with a rule in the accent the web uses for
    # "alive", so a glance tells you which icons move.
    mark = rgb(colors["neon-dim"]["hex"])

    pixels = [[bg] * width for _ in range(height)]
    for row, icon in enumerate(icons.values()):
        top = pad + row * (cell_h + row_gap)
        if icon["clock"] != "static":
            for x in range(pad, width - pad):
                pixels[top + cell_h + row_gap // 2][x] = mark
        for col, rows in enumerate(icon["frames"]):
            left = pad + col * (frame_w + col_gap)
            offset = top
            for scale in scales:
                for y, line in enumerate(rows):
                    for x, ch in enumerate(line):
                        if ch != "#":
                            continue
                        for dy in range(scale):
                            for dx in range(scale):
                                pixels[offset + y * scale + dy][left + x * scale + dx] = fg
                offset += 8 * scale + stack_gap

    raw = b"".join(
        b"\x00" + b"".join(bytes(px) for px in line) for line in pixels
    )

    def chunk(tag: bytes, payload: bytes) -> bytes:
        body = tag + payload
        return (
            struct.pack(">I", len(payload))
            + body
            + struct.pack(">I", binascii.crc32(body) & 0xFFFFFFFF)
        )

    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", header)
        + chunk(b"IDAT", zlib.compress(raw, 9))
        + chunk(b"IEND", b"")
    )


def cpp_ident(name: str) -> str:
    return "".join(part.capitalize() for part in name.replace("-", "_").split("_"))


def emit_theme_gen_hpp(tokens: dict, strings: dict) -> str:
    device = strip_meta(tokens["device"])
    out = [banner("//"), "", "#pragma once", "", "#include <cstdint>", "", "namespace neon::ui {", ""]

    out.append("// 128x128 panel geometry. Every screen positions itself from these -")
    out.append("// no screen may hard-code a pixel offset.")
    for name, value in device.items():
        if isinstance(value, int):
            out.append(f"inline constexpr int k{cpp_ident(name)} = {value};")

    out.append("")
    out.append("// Shared status vocabulary. The web shows the long form of the same")
    out.append("// entry, so the two surfaces never invent synonyms for one state.")
    for name, entry in strip_meta(strings["states"]).items():
        device_word = entry["device"]
        out.append(f'inline constexpr const char kWord{cpp_ident(name)}[] = "{device_word}";')

    out.append("")
    for name, entry in strip_meta(strings["screens"]).items():
        out.append(f'inline constexpr const char kTitle{cpp_ident(name)}[] = "{entry["device"]}";')

    out.append("")
    out.append(f'inline constexpr const char kBrand[] = "{strings["brand"]["device"]}";')
    out.append(f'inline constexpr const char kSetupIp[] = "{strings["brand"]["setup_ip"]}";')
    out.append("")
    out.append("}  // namespace neon::ui")
    return "\n".join(out) + "\n"


def emit_icons_gen_hpp(icons: dict) -> str:
    max_frames = max(len(icon["frames"]) for icon in icons.values())
    out = [
        banner("//"),
        "",
        "#pragma once",
        "",
        "#include <cstdint>",
        "",
        "namespace neon::ui {",
        "",
        "// What advances an icon's frame. Motion is meaning here, so the clock",
        "// is a property of the icon rather than a global setting: kBeat loops",
        "// run at the tempo, kTick loops run at a fixed rate.",
        "enum class IconClock : uint8_t {",
        "  kStatic,",
        "  kBeat,",
        "  kTick,",
        "};",
        "",
        f"inline constexpr int kIconTickHz = {ICON_TICK_HZ};",
        f"inline constexpr int kIconMaxFrames = {max_frames};",
        "",
        "// 8x8 1-bit icons, one byte per row, bit 7 = leftmost pixel. Static",
        "// icons simply have one frame. The 8x8 size itself is kIconSize in",
        "// theme_gen.hpp, which owns panel geometry.",
        "struct Icon {",
        "  IconClock clock;",
        "  uint8_t frame_count;",
        "  uint8_t frames[kIconMaxFrames][8];",
        "};",
        "",
        "// Rows for one frame, wrapping the index. Callers pass a beat or tick",
        "// counter straight in without worrying about an icon's length.",
        "inline constexpr const uint8_t* icon_frame(const Icon& icon,",
        "                                           uint32_t index) {",
        "  return icon.frames[icon.frame_count > 0 ? index % icon.frame_count : 0];",
        "}",
        "",
    ]
    clock_enum = {"static": "IconClock::kStatic", "beat": "IconClock::kBeat", "tick": "IconClock::kTick"}
    for name, icon in icons.items():
        frames = []
        for rows in icon["frames"]:
            packed = []
            for row in rows:
                value = 0
                for x, ch in enumerate(row):
                    if ch == "#":
                        value |= 1 << (7 - x)
                packed.append(f"0x{value:02x}")
            frames.append("{" + ", ".join(packed) + "}")
        # Pad to kIconMaxFrames so every Icon is the same shape; frame_count
        # is what bounds playback.
        while len(frames) < max_frames:
            frames.append("{}")
        out.append(f"inline constexpr Icon kIcon{cpp_ident(name)} = {{")
        out.append(f"    {clock_enum[icon['clock']]}, {len(icon['frames'])},")
        out.append("    {" + ", ".join(frames) + "}};")
    out.append("")
    out.append("}  // namespace neon::ui")
    return "\n".join(out) + "\n"


def emit_hero_font_hpp(spec: dict, raster: "dict[str, tuple[int, list[int]]]") -> str:
    cell_w = spec["cell"]["width"]
    cell_h = spec["cell"]["height"]
    tracking = spec["tracking"]

    out = [
        banner("//"),
        "",
        "#pragma once",
        "",
        "#include <cstdint>",
        "",
        "namespace neon::ui {",
        "",
        "// Seven-segment tempo numerals. Only the glyphs a tempo readout can",
        "// contain: digits, a decimal point, a colon and a dash for the",
        "// not-yet-synced placeholder.",
        f"inline constexpr int kHeroHeight = {cell_h};",
        f"inline constexpr int kHeroMaxWidth = {cell_w};",
        f"inline constexpr int kHeroTracking = {tracking};",
        "",
        "struct HeroGlyph {",
        "  char ch;",
        "  uint8_t width;",
        "  // One entry per column; bit N is row N counting down from the top.",
        f"  uint32_t cols[{cell_w}];",
        "};",
        "",
        f"inline constexpr int kHeroGlyphCount = {len(raster)};",
        "inline constexpr HeroGlyph kHeroGlyphs[kHeroGlyphCount] = {",
    ]
    for ch, (width, cols) in raster.items():
        literal = "'\\''" if ch == "'" else f"'{ch}'"
        padded = list(cols) + [0] * (cell_w - len(cols))
        values = ", ".join(f"0x{c:08x}" for c in padded)
        out.append(f"    {{{literal}, {width}, {{{values}}}}},")
    out.append("};")
    out.append("")
    out.append("// Returns nullptr for a character the hero face does not carry.")
    out.append("inline constexpr const HeroGlyph* hero_glyph(char ch) {")
    out.append("  for (int i = 0; i < kHeroGlyphCount; ++i) {")
    out.append("    if (kHeroGlyphs[i].ch == ch) {")
    out.append("      return &kHeroGlyphs[i];")
    out.append("    }")
    out.append("  }")
    out.append("  return nullptr;")
    out.append("}")
    out.append("")
    out.append("}  // namespace neon::ui")
    return "\n".join(out) + "\n"


# ---------------------------------------------------------------------------
# driver
# ---------------------------------------------------------------------------


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check",
        action="store_true",
        help="verify the checked-in generated files are up to date instead of writing them",
    )
    args = parser.parse_args()

    tokens = load_json(DESIGN / "tokens.json")
    strings = load_json(DESIGN / "strings.json")
    icons = load_icons(DESIGN / "icons.txt")
    hero_spec = load_json(DESIGN / "fonts" / "hero.json")
    hero = raster_hero(hero_spec)

    outputs = {
        ROOT / "web" / "src" / "styles" / "tokens.css": emit_tokens_css(tokens),
        ROOT / "web" / "src" / "design" / "tokens.ts": emit_tokens_ts(tokens),
        ROOT / "web" / "src" / "design" / "strings.ts": emit_strings_ts(strings),
        ROOT / "web" / "src" / "design" / "icons.ts": emit_icons_ts(icons),
        ROOT / "web" / "src" / "design" / "heroFont.ts": emit_hero_font_ts(hero_spec),
        DESIGN / "fonts" / "hero_preview.txt": hero_preview(hero_spec, hero),
        DESIGN / "icons_preview.png": emit_icons_preview_png(tokens, icons),
        ROOT / "components" / "neon_core" / "include" / "neon" / "ui" / "theme_gen.hpp": emit_theme_gen_hpp(tokens, strings),
        ROOT / "components" / "neon_core" / "include" / "neon" / "ui" / "icons_gen.hpp": emit_icons_gen_hpp(icons),
        ROOT / "components" / "neon_core" / "include" / "neon" / "ui" / "hero_font_gen.hpp": emit_hero_font_hpp(hero_spec, hero),
    }

    stale: list[str] = []
    for path, content in outputs.items():
        rel = path.relative_to(ROOT)
        binary = isinstance(content, bytes)
        current = None
        if path.exists():
            current = path.read_bytes() if binary else path.read_text(encoding="utf-8")

        if args.check:
            if current != content:
                stale.append(str(rel))
            continue

        path.parent.mkdir(parents=True, exist_ok=True)
        if current == content:
            print(f"  unchanged  {rel}")
        elif binary:
            path.write_bytes(content)
            print(f"  wrote      {rel}")
        else:
            path.write_text(content, encoding="utf-8")
            print(f"  wrote      {rel}")

    if args.check:
        if stale:
            print("Generated files are out of date:", file=sys.stderr)
            for rel in stale:
                print(f"  {rel}", file=sys.stderr)
            print("\nRun: python3 scripts/gen_design.py", file=sys.stderr)
            return 1
        print(f"All {len(outputs)} generated files are up to date.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
