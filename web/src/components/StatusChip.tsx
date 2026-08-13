import { strings, type StateName } from "../design/strings";
import { Icon } from "./Icon";
import type { IconName } from "../design/icons";

interface Props {
  state: StateName;
  /** Appends a value to the chip, e.g. the peer count or an address. */
  detail?: string;
  title?: string;
  /** Current tempo, so tempo-locked icon loops run at the session's speed. */
  bpm?: number;
}

/**
 * A single machine state.
 *
 * The word, the icon and the accent colour all come from the same entry in
 * design/strings.json — the same entry the firmware reads for the panel's
 * abbreviation. That is what stops the two surfaces drifting into synonyms
 * (DESIGN_SYSTEM.md §6), and pairing colour with a word and an icon is what
 * keeps the state readable without relying on colour alone (§12).
 */
export function StatusChip({ state, detail, title, bpm }: Props) {
  const entry = strings.states[state];
  return (
    <span class={`chip chip--${entry.tone}`} title={title ?? entry.long}>
      <Icon
        name={entry.icon as IconName}
        size={12}
        beatMs={bpm && bpm > 0 ? 60000 / bpm : undefined}
      />
      {entry.web}
      {detail ? <span class="mono">{detail}</span> : null}
    </span>
  );
}
