import { render } from "preact";
import "./styles/base.css";
import { App } from "./app";
import { initTheme, readStoredTheme } from "./theme";
import { tokens } from "./design/tokens";

initTheme();

// Single-file embed cannot fetch /manifest.json. A blob URL is enough
// for Android "Add to Home screen"; iOS uses the apple-mobile-web-app
// tags in index.html.
const pageColor = tokens.themes[readStoredTheme()].color.bg;
const manifest = {
  name: "NEON LINK",
  short_name: "NEON",
  display: "standalone",
  background_color: pageColor,
  theme_color: pageColor,
  start_url: "./",
};
const link = document.createElement("link");
link.rel = "manifest";
link.href = URL.createObjectURL(
  new Blob([JSON.stringify(manifest)], { type: "application/manifest+json" }),
);
document.head.appendChild(link);

const root = document.getElementById("app");
if (root) {
  render(<App />, root);
}
