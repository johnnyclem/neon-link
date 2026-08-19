import { defineConfig, type Plugin } from "vite";
import { viteSingleFile } from "vite-plugin-singlefile";

const MOCK_CLOCK = {
  enabled: true,
  ppqn: 4,
  mult: 1,
  div: 1,
  mode: "trig",
  trig_len_us: 5000,
  duty_pct: 50,
  shuffle_pct: 0,
  rhythm: "all",
  euclid_steps: 16,
  euclid_fills: 4,
  euclid_rot: 0,
  probability_pct: 100,
  humanize_pct: 0,
  role: "clock",
  free_run: false,
  step_mask: "ffffffffffffffff",
  rhythm_over_loop: false,
};

const MOCK_CONFIG = {
  engine: {
    clocks: [
      { ...MOCK_CLOCK },
      { ...MOCK_CLOCK, enabled: false },
      { ...MOCK_CLOCK, enabled: false, role: "gate" },
      { ...MOCK_CLOCK, enabled: false, role: "reset_loop" },
    ],
    reset_mode: "bar",
    reset_trig_len_us: 2000,
    latency_us: 0,
    transport_gating: true,
    reset_before_edge: false,
    reset_lead_us: 0,
  },
  tempo_cv: { min_bpm: 40, max_bpm: 240 },
  quantum: 4,
  clock_source: "auto",
  clock_in_ppqn: 4,
  ble: {
    enabled: true,
    midi_clock_out: false,
    channel: 255,
    gate_target: 255,
    pitch_cv: false,
    cc_latency: 255,
    cc_shuffle_base: 255,
    clock_policy: "ignore",
    transport_enabled: true,
  },
  start_stop_sync: true,
  midi_nudge_us: 0,
  device_name: "neon-link",
  display_brightness: 200,
  big_beat_display: true,
  beat_style: "number",
  tempo_milli_bpm: 120000,
  wifi: { networks: [], retries: 3, ssid: "", pass: "" },
  ap: {
    policy: "fallback",
    ssid: "NEON-LINK",
    pass: "",
    require_pass: false,
    hidden: false,
    channel: 1,
  },
};

const MOCK_STATUS = {
  bpm: 120,
  peers: 0,
  playing: false,
  network: "none",
  ext_clock: false,
  uptime_s: 0,
  phase_milli: 0,
  quantum: 4,
  tempo_valid: true,
  hostname: "neon-link",
  ip: "127.0.0.1",
  setup_ap: false,
  wifi_ssid: "",
  wifi_pass_len: 0,
  wifi_fail_reason: 0,
  firmware: "dev",
  device_name: "neon-link",
  ap_ssid: "",
  set_bpm: 120,
  pulse: { edges: 0, late_max_us: 0, late_avg_us: 0 },
};

/** Local UI work without a module on the LAN. `NEON_MOCK=1 npm run dev`. */
function mockApi(): Plugin {
  return {
    name: "neon-mock-api",
    configureServer(server) {
      if (!process.env.NEON_MOCK) return;
      server.middlewares.use("/api", (req, res) => {
        res.setHeader("Content-Type", "application/json");
        if (req.url?.startsWith("/config") && req.method === "PUT") {
          let body = "";
          req.on("data", (c) => (body += c));
          req.on("end", () => {
            res.end(body || JSON.stringify(MOCK_CONFIG));
          });
          return;
        }
        if (req.url?.startsWith("/config")) {
          res.end(JSON.stringify(MOCK_CONFIG));
          return;
        }
        if (req.url?.startsWith("/status")) {
          const elapsed = Date.now() / 1000;
          const bpm = 120;
          const quantum = 4;
          const beats = (elapsed * bpm) / 60;
          res.end(
            JSON.stringify({
              ...MOCK_STATUS,
              playing: true,
              bpm,
              quantum,
              phase_milli: Math.floor((beats % quantum) * 1000),
            }),
          );
          return;
        }
        res.statusCode = 404;
        res.end(JSON.stringify({ ok: false }));
      });
    },
  };
}

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
  plugins: [mockApi(), viteSingleFile()],
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
