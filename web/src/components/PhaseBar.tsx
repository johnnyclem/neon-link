import { tokens } from "../design/tokens";

const D = tokens.device;

interface Props {
  /** Position within the bar, 0..1. */
  phase: number;
  quantum: number;
  running: boolean;
  height?: number;
}

/**
 * The phase bar, drawn in the device's own coordinate space.
 *
 * The viewBox is literally 128 units wide and kBarH units tall, with the
 * same inset and the same beat ticks the firmware draws. That is what makes
 * the proportions match 1:1 rather than approximately (DESIGN_SYSTEM.md
 * open decision §15.4).
 *
 * When the transport is stopped the fill switches to a checkerboard — the
 * same half-tone dither the panel uses, for the same reason: a held position
 * should be visibly not advancing.
 */
export function PhaseBar({ phase, quantum, running, height = 22 }: Props) {
  const clamped = Math.max(0, Math.min(1, phase));
  const track = D.width - 2 * D.bar_inset;
  const fill = track * clamped;
  const ticks = Array.from({ length: Math.max(0, quantum - 1) }, (_, i) => (D.width * (i + 1)) / quantum);

  // Natural aspect, never stretched. The tokens declare this bar as 128 x 16
  // with a 3-unit inset; distorting it horizontally would make the fill, the
  // ticks and the dither all lie about the panel's proportions, which is the
  // one thing this component exists to preserve.
  const viewH = D.bar_h + 2 * D.bar_tick_h;
  const width = (height * D.width) / viewH;

  return (
    <svg
      class="phasebar"
      height={height}
      width={width}
      viewBox={`0 ${-D.bar_tick_h} ${D.width} ${viewH}`}
      shape-rendering="crispEdges"
      role="img"
      aria-label={`Bar position ${Math.round(clamped * quantum * 10) / 10} of ${quantum} beats`}
    >
      <defs>
        {/* The panel's 50% dither, as an SVG pattern. */}
        <pattern id="phase-dither" width="2" height="2" patternUnits="userSpaceOnUse">
          <rect x="0" y="0" width="1" height="1" fill="var(--neon)" />
          <rect x="1" y="1" width="1" height="1" fill="var(--neon)" />
        </pattern>
      </defs>

      {/* The bar stretches to its container, but its rules must not: a 1px
          hairline on the panel has to stay a hairline here, or the ticks
          become blocks and the proportions stop matching. */}
      <rect
        x={0.5}
        y={0.5}
        width={D.width - 1}
        height={D.bar_h - 1}
        fill="none"
        stroke="var(--text-muted)"
        stroke-width={1}
        vector-effect="non-scaling-stroke"
      />

      {fill > 0 ? (
        <rect
          x={D.bar_inset}
          y={D.bar_inset}
          width={fill}
          height={D.bar_h - 2 * D.bar_inset}
          fill={running ? "var(--neon)" : "url(#phase-dither)"}
        />
      ) : null}

      {ticks.map((x) => (
        <g key={x} stroke="var(--text-muted)" stroke-width={1} vector-effect="non-scaling-stroke">
          <line x1={x} y1={-D.bar_tick_h} x2={x} y2={0} />
          <line x1={x} y1={D.bar_h} x2={x} y2={D.bar_h + D.bar_tick_h} />
        </g>
      ))}
    </svg>
  );
}
