import { iconMasters, ICON_SIZE, type IconName } from "../design/icons";

interface Props {
  name: IconName;
  /** Rendered size in px. Multiples of 8 keep the pixel grid exact. */
  size?: number;
  title?: string;
}

/**
 * Draws the device's 8x8 1-bit icon master as an SVG pixel grid.
 *
 * These are deliberately not redrawn as smooth vectors. Showing the panel's
 * actual pixels is the most direct way to make the page and the module read
 * as one object (DESIGN_SYSTEM.md §7), and at 2x/3x the pixel grid is a
 * legible style rather than an artefact.
 */
export function Icon({ name, size = 16, title }: Props) {
  const rows = iconMasters[name];
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
      {rows.flatMap((row, y) =>
        [...row].map((cell, x) =>
          cell === "#" ? <rect key={`${x}-${y}`} x={x} y={y} width={1} height={1} /> : null,
        ),
      )}
    </svg>
  );
}
