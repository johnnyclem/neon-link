import { defineConfig } from "vite";
import { viteSingleFile } from "vite-plugin-singlefile";

// Device build. Everything is inlined into one HTML file: the module serves
// this from its own access point during setup, so there is no second request
// to make and no CDN to reach.
//
// The style guide is deliberately NOT part of this build - it has its own
// config and entry point, so the simulator, the screen fixtures and the
// swatch pages can never end up in flash.
export default defineConfig({
  root: ".",
  build: {
    outDir: "dist",
    emptyOutDir: true,
    // The ESP32's HTTP server streams one response; a single small file
    // beats chunking a bundle over a 2.4 GHz link the user is mid-setup on.
    assetsInlineLimit: 100_000_000,
    cssCodeSplit: false,
    target: "es2020",
    reportCompressedSize: true,
  },
  plugins: [viteSingleFile()],
  esbuild: {
    jsx: "automatic",
    jsxImportSource: "preact",
  },
  resolve: {
    alias: {
      react: "preact/compat",
      "react-dom": "preact/compat",
    },
  },
  server: {
    // `npm run dev` against a real module: point this at its address and the
    // API calls go to hardware while the UI hot-reloads.
    proxy: {
      "/api": {
        target: process.env.NEON_DEVICE ?? "http://192.168.4.1",
        changeOrigin: true,
      },
    },
  },
});
