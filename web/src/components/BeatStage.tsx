/**
 * The web echo of the panel's full-screen beat stage.
 *
 * Number is the original giant 1/2/3/4. Pie fills a slice per beat,
 * pendulum swings on the beat grid, pulse blooms from the centre.
 * Phase is interpolated between /api/status polls so the motion
 * keeps time instead of waiting on the 1 s tick.
 */
import { useEffect, useState } from "preact/hooks";

export type BeatStyle = "number" | "pie" | "pendulum" | "pulse";

export const BEAT_STYLES: { id: BeatStyle; label: string }[] = [
  { id: "number", label: "Number" },
  { id: "pie", label: "Pie" },
  { id: "pendulum", label: "Pendulum" },
  { id: "pulse", label: "Pulse" },
];

export function beatStyleFrom(value: unknown): BeatStyle {
  if (value === "pie" || value === "pendulum" || value === "pulse" || value === "number") {
    return value;
  }
  return "number";
}

export function nextBeatStyle(style: BeatStyle): BeatStyle {
  const i = BEAT_STYLES.findIndex((s) => s.id === style);
  return BEAT_STYLES[(i + 1) % BEAT_STYLES.length].id;
}

export function beatFromStatus(phaseMilli: number, quantum: number): number {
  const q = quantum !== 0 ? quantum : 4;
  return (Math.floor(phaseMilli / 1000) % q) + 1;
}

function useLivePhase(
  phaseMilli: number,
  bpm: number,
  quantum: number,
  playing: boolean,
): { beat: number; frac: number; beats: number; quantum: number } {
  const [nowPhase, setNowPhase] = useState(phaseMilli);
  const q = quantum !== 0 ? quantum : 4;

  useEffect(() => {
    if (!playing) {
      setNowPhase(phaseMilli);
      return;
    }
    const t0 = performance.now();
    const p0 = phaseMilli;
    let raf = 0;
    const tick = (t: number) => {
      const advanced = ((t - t0) * bpm) / 60;
      const span = q * 1000;
      let next = p0 + advanced * 1000;
      next = ((next % span) + span) % span;
      setNowPhase(next);
      raf = requestAnimationFrame(tick);
    };
    raf = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(raf);
  }, [phaseMilli, bpm, q, playing]);

  const beats = nowPhase / 1000;
  const beat = (Math.floor(beats) % q) + 1;
  const frac = beats - Math.floor(beats);
  return { beat, frac, beats, quantum: q };
}

function piePath(cx: number, cy: number, r: number, endDeg: number): string {
  if (endDeg >= 359.5) {
    return "";
  }
  const start = -Math.PI / 2;
  const end = start + (endDeg * Math.PI) / 180;
  const x0 = cx + r * Math.cos(start);
  const y0 = cy + r * Math.sin(start);
  const x1 = cx + r * Math.cos(end);
  const y1 = cy + r * Math.sin(end);
  const large = endDeg > 180 ? 1 : 0;
  return `M ${cx} ${cy} L ${x0} ${y0} A ${r} ${r} 0 ${large} 1 ${x1} ${y1} Z`;
}

function NumberViz({ beat }: { beat: number }) {
  return (
    <span key={beat} class="beat-stage__n">
      {beat}
    </span>
  );
}

function PieViz({ beat, quantum }: { beat: number; quantum: number }) {
  const end = (beat / quantum) * 360;
  const ticks = Array.from({ length: quantum }, (_, i) => (i / quantum) * 2 * Math.PI);
  return (
    <svg class="beat-stage__viz" viewBox="0 0 100 100" aria-hidden="true">
      <circle cx="50" cy="50" r="42" fill="none" stroke="currentColor" stroke-width="1.5" />
      {end >= 359.5 ? (
        <circle cx="50" cy="50" r="42" fill="currentColor" />
      ) : end > 0.5 ? (
        <path d={piePath(50, 50, 42, end)} fill="currentColor" />
      ) : null}
      {ticks.map((a) => (
        <line
          key={a}
          x1="50"
          y1="50"
          x2={50 + 42 * Math.sin(a)}
          y2={50 - 42 * Math.cos(a)}
          stroke="var(--bg)"
          stroke-width="1.5"
        />
      ))}
      <circle cx="50" cy="50" r="10" fill="var(--bg)" />
      <circle cx="50" cy="50" r="42" fill="none" stroke="currentColor" stroke-width="1.5" />
    </svg>
  );
}

function PendulumViz({ beats }: { beats: number }) {
  const deg = -32 * Math.cos(Math.PI * beats);
  return (
    <svg class="beat-stage__viz" viewBox="0 0 100 100" aria-hidden="true">
      <rect x="46" y="8" width="8" height="8" fill="currentColor" />
      <g transform={`rotate(${deg} 50 12)`}>
        <rect x="48.5" y="12" width="3" height="60" fill="currentColor" />
        <rect x="42" y="68" width="16" height="16" fill="currentColor" />
      </g>
    </svg>
  );
}

function PulseViz({ frac, accent }: { frac: number; accent: boolean }) {
  const r = 10 + frac * 34;
  const opacity = Math.max(0, 1 - frac);
  return (
    <svg class="beat-stage__viz" viewBox="0 0 100 100" aria-hidden="true">
      <circle cx="50" cy="50" r="44" fill="none" stroke="currentColor" stroke-width="1" opacity="0.25" />
      <circle
        cx="50"
        cy="50"
        r={r}
        fill="none"
        stroke="currentColor"
        stroke-width="3"
        opacity={opacity}
      />
      {accent ? (
        <circle
          cx="50"
          cy="50"
          r={r * 0.55}
          fill="none"
          stroke="currentColor"
          stroke-width="2"
          opacity={opacity}
        />
      ) : null}
      {frac < 0.18 ? <rect x="42" y="42" width="16" height="16" fill="currentColor" /> : null}
    </svg>
  );
}

export function BeatStage({
  phaseMilli,
  quantum,
  bpm,
  playing,
  style = "number",
  onCycle,
}: {
  phaseMilli: number;
  quantum: number;
  bpm: number;
  playing: boolean;
  style?: BeatStyle;
  onCycle?: () => void;
}) {
  const live = useLivePhase(phaseMilli, bpm, quantum, playing);
  if (!playing) {
    return null;
  }

  const viz =
    style === "pie" ? (
      <PieViz beat={live.beat} quantum={live.quantum} />
    ) : style === "pendulum" ? (
      <PendulumViz beats={live.beats} />
    ) : style === "pulse" ? (
      <PulseViz frac={live.frac} accent={live.beat === 1} />
    ) : (
      <NumberViz beat={live.beat} />
    );

  return (
    <div
      class="beat-stage"
      data-style={style}
      data-downbeat={live.beat === 1 ? "true" : "false"}
      aria-live="polite"
      aria-label={`Beat ${live.beat}, ${style}`}
      role={onCycle ? "button" : undefined}
      tabIndex={onCycle ? 0 : undefined}
      onClick={onCycle}
      onKeyDown={
        onCycle
          ? (e) => {
              if (e.key === "Enter" || e.key === " ") {
                e.preventDefault();
                onCycle();
              }
            }
          : undefined
      }
    >
      {viz}
    </div>
  );
}
