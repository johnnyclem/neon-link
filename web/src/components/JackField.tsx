import { Fragment } from "preact";
import type { ClockConfig, Config } from "../api";

/** One hole on the AMYboard 5×2 Thonkiconn block. */
export type JackId =
  | "spdif-in"
  | "spdif-out"
  | "line-in"
  | "line-out"
  | "midi-in"
  | "midi-out"
  | "cv1-in"
  | "cv1-out"
  | "cv2-in"
  | "cv2-out";

const ROWS: { label: string; left: JackId; right: JackId }[] = [
  { label: "SPDIF", left: "spdif-in", right: "spdif-out" },
  { label: "LINE", left: "line-in", right: "line-out" },
  { label: "MIDI", left: "midi-in", right: "midi-out" },
  { label: "CV 1", left: "cv1-in", right: "cv1-out" },
  { label: "CV 2", left: "cv2-in", right: "cv2-out" },
];

export function jackTitle(id: JackId): string {
  switch (id) {
    case "spdif-in":
      return "SPDIF IN";
    case "spdif-out":
      return "SPDIF OUT";
    case "line-in":
      return "LINE IN";
    case "line-out":
      return "LINE OUT";
    case "midi-in":
      return "MIDI IN";
    case "midi-out":
      return "MIDI OUT";
    case "cv1-in":
      return "CV IN 1";
    case "cv1-out":
      return "CV OUT 1";
    case "cv2-in":
      return "CV IN 2";
    case "cv2-out":
      return "CV OUT 2";
  }
}

function roleTag(c: ClockConfig): string {
  if (!c.enabled) return "OFF";
  switch (c.role) {
    case "gate":
      return "GATE";
    case "reset_loop":
      return "RST";
    case "reset_start":
      return "RST";
    case "reset_stop":
      return "RST";
    default:
      return "CLK";
  }
}

/** Short tag for what an audio output channel carries. */
function audioRoleTag(role: Config["audio"]["role_l"]): string {
  switch (role) {
    case "metronome":
      return "METRO";
    case "clock":
      return "CLK";
    case "reset":
      return "RST";
    case "run":
      return "RUN";
    case "synth":
      return "SYNTH";
    case "link_in":
      return "LINK";
    case "line_in":
      return "LINE";
    default:
      return "MIX";
  }
}

/** What NEON LINK actually puts on each hole. */
export function jackLive(id: JackId, cfg: Config): string {
  switch (id) {
    case "cv1-out":
      return cfg.ble.pitch_cv ? "PITCH" : "TEMPO";
    case "cv2-out":
      return roleTag(cfg.engine.clocks[0]);
    case "cv1-in":
      return "CLK IN";
    case "cv2-in":
      return "RST IN";
    case "midi-out":
      return cfg.ble.midi_clock_out ? "MCLK" : "MIDI";
    case "midi-in":
      return "IN";
    case "line-out":
      if (!cfg.audio.enabled) return "OFF";
      return cfg.audio.role_l === cfg.audio.role_r
        ? audioRoleTag(cfg.audio.role_l)
        : "L·R";
    case "line-in":
      return cfg.audio.enabled ? "LINE" : "OFF";
    default:
      // SPDIF is on the board but deliberately deferred — say so, rather
      // than a dash that reads as a rendering fault.
      return "SOON";
  }
}

export function jackWired(id: JackId): boolean {
  // Everything but SPDIF is driven now that the audio engine exists; the
  // line pair is the codec's I/O.
  return id !== "spdif-in" && id !== "spdif-out";
}

/**
 * Front-of-module AMYboard jack field. IN is the left column, OUT the
 * right — same 5×2 block as the hardware. The neon ring is the hole
 * whose settings are open below.
 */
export function JackField({
  cfg,
  selected,
  onSelect,
}: {
  cfg: Config;
  selected: JackId;
  onSelect: (id: JackId) => void;
}) {
  return (
    <div class="jacks" role="group" aria-label="AMYboard jacks">
      <span class="jacks__corner" />
      <span class="jacks__col">In</span>
      <span class="jacks__col">Out</span>
      {ROWS.map((row) => (
        <Fragment key={row.label}>
          <span class="jacks__row">{row.label}</span>
          <JackButton
            id={row.left}
            tag={jackLive(row.left, cfg)}
            selected={selected === row.left}
            wired={jackWired(row.left)}
            onSelect={onSelect}
          />
          <JackButton
            id={row.right}
            tag={jackLive(row.right, cfg)}
            selected={selected === row.right}
            wired={jackWired(row.right)}
            onSelect={onSelect}
          />
        </Fragment>
      ))}
    </div>
  );
}

function JackButton({
  id,
  tag,
  selected,
  wired,
  onSelect,
}: {
  id: JackId;
  tag: string;
  selected: boolean;
  wired: boolean;
  onSelect: (id: JackId) => void;
}) {
  return (
    <button
      type="button"
      class={`jack${selected ? " is-on" : ""}${wired ? "" : " is-dead"}`}
      aria-pressed={selected}
      aria-label={`${id.replace("-", " ")} ${tag}`}
      onClick={() => onSelect(id)}
    >
      <span class="jack__hole" aria-hidden="true" />
      <span class="jack__tag">{tag}</span>
    </button>
  );
}
