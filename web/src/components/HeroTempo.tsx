import { heroFont } from "../design/heroFont";

const SEGMENTS = Object.entries(heroFont.segments);

interface Props {
  /** Tempo in BPM, or null before the first sync. */
  bpm: number | null;
  /** Rendered glyph height in px. */
  size?: number;
  unit?: boolean;
}

/**
 * The tempo readout.
 *
 * This is not a font that resembles the device's numerals — it is the same
 * segment map from design/fonts/hero.json, rendered as SVG. The browser adds
 * the one thing a 1-bit panel cannot: the unlit segments, drawn as ghosts,
 * the way an LED readout actually looks up close.
 */
export function HeroTempo({ bpm, size = 64, unit = true }: Props) {
  const text = bpm === null ? "--.-" : bpm.toFixed(1);
  const { width: cellW, height: cellH } = heroFont.cell;

  let x = 0;
  const parts: preact.JSX.Element[] = [];
  for (const ch of text) {
    const glyph = heroFont.glyphs[ch as keyof typeof heroFont.glyphs];
    if (!glyph) continue;
    if (x > 0) x += heroFont.tracking;

    if (glyph.on.length > 0) {
      const lit = new Set<string>(glyph.on);
      for (const [name, r] of SEGMENTS) {
        parts.push(
          <rect
            key={`${x}-${name}`}
            x={x + r.x}
            y={r.y}
            width={r.w}
            height={r.h}
            fill={lit.has(name) ? "var(--hero)" : "var(--surface-2)"}
          />,
        );
      }
    }
    for (const [i, r] of glyph.rects.entries()) {
      parts.push(
        <rect
          key={`${x}-r${i}`}
          x={x + r.x}
          y={r.y}
          width={r.w}
          height={r.h}
          fill="var(--hero)"
        />,
      );
    }
    x += glyph.width ?? cellW;
  }

  return (
    <div class="hero">
      <svg
        class="hero__digits"
        height={size}
        width={(size * x) / cellH}
        viewBox={`0 0 ${x} ${cellH}`}
        shape-rendering="crispEdges"
        role="img"
        aria-label={bpm === null ? "Tempo not yet synced" : `${text} BPM`}
      >
        {parts}
      </svg>
      {unit ? <span class="hero__unit">BPM</span> : null}
    </div>
  );
}
