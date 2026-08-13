import { Button } from "../components/controls";
import type { PageProps } from "../app";

/**
 * Sticks to the bottom of the viewport on every page that edits config.
 *
 * The module is usually configured from a phone standing next to the rack;
 * a SAVE button that scrolled off the end of a long form is how settings
 * get lost.
 *
 * The bar only exists while it has something to say: unsaved changes, a
 * save in flight, or a result. A clean page gets the row of pixels back —
 * a permanent bar restating that saving works was headroom spent on a
 * constant.
 */
export function SaveBar({ save, dirty, saving, message }: PageProps) {
  if (!dirty && !saving && !message) return null;

  return (
    <div class="savebar">
      <div class="btn-row" style="margin-top:0">
        {dirty || saving ? (
          <Button type="button" onClick={() => void save()} disabled={!dirty || saving}>
            {saving ? "Saving…" : "Save"}
          </Button>
        ) : null}
        {message ? (
          <span
            class={`btn-row__msg btn-row__msg--${message.kind === "ok" ? "ok" : "err"}`}
            role="status"
          >
            {message.text}
          </span>
        ) : (
          <span class="btn-row__msg">{dirty ? "Unsaved changes" : ""}</span>
        )}
      </div>
    </div>
  );
}
