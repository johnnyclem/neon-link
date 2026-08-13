import { iconMasters, ICON_SIZE, ICON_TICK_HZ, type IconName } from "../design/icons";

interface Props {
  name: IconName;
  /** Rendered size in px. Multiples of 8 keep the pixel grid exact. */
  size?: number;
  title?: string;
  /**
   * Milliseconds per beat, for icons whose loop is tempo-locked. Supply it
   * wherever the tempo is known and the beat-clocked icons will run at the
   * session's speed, exactly as the panel's do. Without it they fall back to
   * a neutral 120 BPM rather than freezing, so an icon in a style guide or a
   * disconnected page still reads as animated.
   */
  beatMs?: number;
}

const DEFAULT_BEAT_MS = 500;

/**
 * Draws the device's 8x8 1-bit icon master as an SVG pixel grid, and plays
 * the same loop the panel plays.
 *
 * These are deliberately not redrawn as smooth vectors, and the animation is
 * deliberately not a separate web-side effect: the frames come from
 * design/icons.txt, so the page and the panel are showing one artwork moving
 * one way (DESIGN_SYSTEM.md §7). Frames are laid side by side in the SVG and
 * stepped through with a CSS steps() animation, which is a hard cut per frame
 * — no tweening, matching a 1-bit panel that cannot fade.
 */
export function Icon({ name, size = 16, title, beatMs }: Props) {
  const master = iconMasters[name];
  const frames = master.frames as readonly (readonly string[])[];
  const animated = master.clock !== "static" && frames.length > 1;

  const durationMs =
    master.clock === "beat"
      ? frames.length * (beatMs ?? DEFAULT_BEAT_MS)
      : (frames.length * 1000) / ICON_TICK_HZ;

  const cells = frames.flatMap((rows, frame) =>
    rows.flatMap((row, y) =>
      [...row].map((cell, x) =>
        cell === "#" ? (
          <rect
            key={`${frame}-${x}-${y}`}
            x={frame * ICON_SIZE + x}
            y={y}
            width={1}
            height={1}
          />
        ) : null,
      ),
    ),
  );

  return (
    <svg
      class="icon"
      width={size}
      height={size}
      viewBox={`0 0 ${ICON_SIZE} ${ICON_SIZE}`}
      fill="currentColor"
      shape-rendering="crispEdges"
      role={title ? "img" : "presentation"}
      aria-hidden={title ? undefined : "true"}
    >
      {title ? <title>{title}</title> : null}
      {animated ? (
        <g
          class="icon__strip"
          style={`--icon-frames:${frames.length};--icon-shift:${
            -ICON_SIZE * frames.length
          };--icon-duration:${durationMs}ms`}
        >
          {cells}
        </g>
      ) : (
        cells
      )}
    </svg>
  );
}
