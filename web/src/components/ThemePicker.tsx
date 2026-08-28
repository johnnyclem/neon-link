import { useState } from "preact/hooks";
import { tokens, type ThemeId } from "../design/tokens";
import { applyTheme, readStoredTheme } from "../theme";

interface Props {
  value?: ThemeId;
  onChange?: (id: ThemeId) => void;
}

/** Swatch grid of the palettes. Applies immediately; persist via Save / the dial. */
export function ThemePicker({ value, onChange }: Props) {
  const [current, setCurrent] = useState<ThemeId>(() => value ?? readStoredTheme());
  const selected = value ?? current;

  const pick = (id: ThemeId) => {
    applyTheme(id);
    setCurrent(id);
    onChange?.(id);
  };

  return (
    <div class="theme-picker" role="radiogroup" aria-label="Color theme">
      {tokens.themeOrder.map((id) => {
        const theme = tokens.themes[id];
        const c = theme.color;
        return (
          <button
            key={id}
            type="button"
            class="theme-pick"
            role="radio"
            aria-checked={selected === id}
            title={theme.note}
            onClick={() => pick(id)}
          >
            <span class="theme-pick__swatch" aria-hidden="true">
              <i style={`background:${c.bg}`} />
              <i style={`background:${c.hero}`} />
              <i style={`background:${c.surface}`} />
              <i style={`background:${c.magenta}`} />
            </span>
            <span class="theme-pick__label">{theme.label}</span>
          </button>
        );
      })}
    </div>
  );
}
