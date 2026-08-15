import { useMemo } from "preact/hooks";
import screensData from "../../../design/screens.json";

interface Screen {
  id: string;
  title: string;
  note: string;
  bits: string;
}

const data = screensData as unknown as {
  width: number;
  height: number;
  screens: Screen[];
  compact: { width: number; height: number; screens: Screen[] };
};

export const screens = data.screens;
export const compactScreens = data.compact.screens;

/**
 * Unpacks the panel's own framebuffer format.
 *
 * 16 pages of 128 columns, one byte per column holding 8 vertical pixels
 * with the LSB on top. This is the byte layout the SH1107 receives, decoded
 * here rather than converted at export time — if the packing ever changes,
 * this breaks loudly instead of drifting quietly.
 */
function unpack(base64: string, width: number, height: number): Uint8Array {
  const binary = atob(base64);
  const out = new Uint8Array(width * height);
  for (let y = 0; y < height; y++) {
    const page = y >> 3;
    const bit = y & 7;
    for (let x = 0; x < width; x++) {
      const byte = binary.charCodeAt(page * width + x);
      out[y * width + x] = (byte >> bit) & 1;
    }
  }
  return out;
}

/**
 * Renders a captured device screen at 1:1 pixel fidelity.
 *
 * These frames come from host/sim/neon_screens.cpp, which drives the
 * firmware's own render_ui(). Nothing here redraws the panel's design — it
 * only displays what the panel would have drawn.
 */
export function DeviceSim({
  screen,
  scale = 2,
  compact = false,
}: {
  screen: Screen;
  scale?: number;
  compact?: boolean;
}) {
  const width = compact ? data.compact.width : data.width;
  const height = compact ? data.compact.height : data.height;
  const pixels = useMemo(() => unpack(screen.bits, width, height), [screen.bits, width, height]);

  const rects: preact.JSX.Element[] = [];
  // Run-length the lit pixels along each row: a 128x128 panel would
  // otherwise be up to 16k individual rects.
  for (let y = 0; y < height; y++) {
    let run = -1;
    for (let x = 0; x <= width; x++) {
      const lit = x < width && pixels[y * width + x] === 1;
      if (lit && run < 0) run = x;
      if (!lit && run >= 0) {
        rects.push(<rect key={`${y}-${run}`} x={run} y={y} width={x - run} height={1} />);
        run = -1;
      }
    }
  }

  return (
    <figure style="margin:0">
      <svg
        width={width * scale}
        height={height * scale}
        viewBox={`0 0 ${width} ${height}`}
        shape-rendering="crispEdges"
        role="img"
        aria-label={screen.title}
        style="background:var(--bg);border:2px solid var(--border);display:block;max-width:100%;height:auto"
      >
        {/* The panel is monochrome; the accent is the phosphor colour, which
            is the same token the web uses for a lit readout. */}
        <g fill="var(--neon)">{rects}</g>
      </svg>
      <figcaption style="margin-top:var(--space-2)">
        <strong style="display:block;font-size:var(--text-label);letter-spacing:var(--tracking-token);text-transform:uppercase">
          {screen.title}
        </strong>
        <span style="color:var(--text-muted);font-size:var(--text-caption)">{screen.note}</span>
      </figcaption>
    </figure>
  );
}
