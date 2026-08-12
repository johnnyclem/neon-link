import { Button } from "../components/controls";
import type { PageProps } from "../app";

/**
 * Sticks to the bottom of the viewport on every page that edits config.
 *
 * The module is usually configured from a phone standing next to the rack;
 * a SAVE button that scrolled off the end of a long form is how settings
 * get lost.
 */
export function SaveBar({ save, dirty, saving, message }: PageProps) {
  return (
    <div class="savebar">
      <div class="btn-row" style="margin-top:0">
        <Button type="button" onClick={() => void save()} disabled={!dirty || saving}>
          {saving ? "Saving…" : dirty ? "Save" : "Saved"}
        </Button>
        {message ? (
          <span class={`btn-row__msg btn-row__msg--${message.kind === "ok" ? "ok" : "err"}`}>
            {message.text}
          </span>
        ) : (
          <span class="btn-row__msg">
            {dirty ? "Unsaved changes" : "Settings persist to the module immediately on save."}
          </span>
        )}
      </div>
    </div>
  );
}
