import { defineConfig } from "vite";
import { viteSingleFile } from "vite-plugin-singlefile";
import { resolve } from "node:path";

// Style guide build - for review and for publishing, never for the device.
// Keeping it in a separate config with its own entry point is what
// guarantees the screen fixtures and swatch pages cannot reach flash.
export default defineConfig({
  root: ".",
  build: {
    outDir: "dist-styleguide",
    emptyOutDir: true,
    assetsInlineLimit: 100_000_000,
    cssCodeSplit: false,
    target: "es2020",
    rollupOptions: {
      input: resolve(__dirname, "styleguide.html"),
    },
  },
  plugins: [viteSingleFile()],
  esbuild: {
    jsx: "automatic",
    jsxImportSource: "preact",
  },
});
