/**
 * The web echo of the panel's full-screen 1 / 2 / 3 / 4.
 *
 * Odd beats are light on dark; even beats invert — same rule as
 * `draw_giant_beat` on the 128×128. Shown only while playing.
 */
export function BeatStage({
  beat,
  playing,
}: {
  beat: number;
  playing: boolean;
}) {
  if (!playing) {
    return null;
  }
  const even = beat % 2 === 0;
  return (
    <div
      class={`beat-stage${even ? " beat-stage--even" : " beat-stage--odd"}`}
      aria-live="polite"
      aria-label={`Beat ${beat}`}
    >
      <span class="beat-stage__n">{beat}</span>
    </div>
  );
}

export function beatFromStatus(phaseMilli: number, quantum: number): number {
  const q = quantum !== 0 ? quantum : 4;
  return (Math.floor(phaseMilli / 1000) % q) + 1;
}
