import { tokens, type ThemeId } from "./design/tokens";

export type { ThemeId };

export const THEME_STORAGE_KEY = "neon-link-theme";

export function isThemeId(value: unknown): value is ThemeId {
  return typeof value === "string" && value in tokens.themes;
}

export function readStoredTheme(): ThemeId {
  try {
    const stored = localStorage.getItem(THEME_STORAGE_KEY);
    if (isThemeId(stored)) return stored;
  } catch {
    // Private mode, or the page is being served from a file:// embed.
  }
  return tokens.defaultTheme;
}

export function applyTheme(id: ThemeId): void {
  const theme = tokens.themes[id];
  const root = document.documentElement;
  root.dataset.theme = id;
  root.style.colorScheme = theme.scheme;
  try {
    localStorage.setItem(THEME_STORAGE_KEY, id);
  } catch {
    // Same as read: persistence is best-effort.
  }
  const scheme = document.querySelector('meta[name="color-scheme"]');
  if (scheme) scheme.setAttribute("content", theme.scheme);
  const color = document.querySelector('meta[name="theme-color"]');
  if (color) color.setAttribute("content", theme.color.bg);
}

export function initTheme(): void {
  applyTheme(readStoredTheme());
}
