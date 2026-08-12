/**
 * In-page section switcher. Used for repeated things (CLK 1–4, Wi-Fi
 * slots) and for grouping a long settings page so a phone never has to
 * scroll a stack of cards to find one control.
 */
export function SubNav({
  items,
  value,
  onChange,
  label,
}: {
  items: readonly { id: string; label: string }[];
  value: string;
  onChange: (id: string) => void;
  label: string;
}) {
  return (
    <div class="subnav" role="tablist" aria-label={label}>
      {items.map((it) => {
        const on = it.id === value;
        return (
          <button
            key={it.id}
            type="button"
            role="tab"
            class="subnav__item"
            aria-selected={on}
            onClick={() => onChange(it.id)}
          >
            {it.label}
          </button>
        );
      })}
    </div>
  );
}
