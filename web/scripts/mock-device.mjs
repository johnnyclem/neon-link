/**
 * A stand-in for the module, for developing the UI without hardware.
 *
 *   node scripts/mock-device.mjs        # serves web/dist on :8123
 *
 * It answers the same four endpoints the firmware does, with a tempo and a
 * phase that actually advance so the strip can be judged in motion. It is a
 * development aid, not a simulator: the authority on device behaviour is the
 * firmware, and the authority on the panel's rendering is design/screens.json.
 */

import { createServer } from "node:http";
import { readFileSync, existsSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const bundle = resolve(here, "../dist/index.html");
const port = Number(process.env.PORT ?? 8123);

const started = Date.now();
let config = {
  engine: {
    clocks: [4, 2, 1, 24].map((ppqn) => ({
      enabled: true,
      ppqn,
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
    })),
    reset_mode: "start",
    reset_trig_len_us: 5000,
    latency_us: 0,
    transport_gating: false,
    reset_before_edge: false,
    reset_lead_us: 1000,
  },
  tempo_cv: { min_bpm: 20, max_bpm: 300 },
  quantum: 4,
  clock_source: "auto",
  clock_in_ppqn: 4,
  ble: {
    enabled: true,
    midi_clock_out: true,
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
  display_brightness: 255,
  big_beat_display: true,
  audio: {
    enabled: true,
    role_l: "mix",
    role_r: "mix",
    metro_enabled: true,
    metro_sound: "sine",
    metro_gain: 200,
    metro_accent: true,
    amy_enabled: false,
    amy_gain: 200,
    amy_patch: 0,
    linein_monitor_gain: 0,
    publish_mix: true,
    publish_linein: false,
    publish_mono: false,
    sub_gain: 200,
    jitter_ms: 60,
    channel_name: "",
    sub_channel_id: "",
  },
  tempo_milli_bpm: 128000,
  wifi: {
    networks: [
      { ssid: process.env.MOCK_SSID ?? "", pass: "", has_pass: false, hidden: false },
      { ssid: "", pass: "", has_pass: false, hidden: false },
      { ssid: "", pass: "", has_pass: false, hidden: false },
      { ssid: "", pass: "", has_pass: false, hidden: false },
    ],
    retries: 3,
    ssid: process.env.MOCK_SSID ?? "",
    pass: "",
  },
  ap: {
    policy: "fallback",
    ssid: "",
    pass: "",
    has_pass: true,
    require_pass: false,
    hidden: false,
    channel: 1,
  },
};

const setupAp = process.env.MOCK_SETUP === "1";
const bpm = 128;
let playing = true;

const status = () => {
  const elapsed = (Date.now() - started) / 1000;
  const beats = (elapsed * bpm) / 60;
  const quantum = config.quantum;
  return {
    bpm,
    peers: setupAp ? 0 : 2,
    playing,
    network: setupAp ? "none" : "wifi",
    ext_clock: false,
    uptime_s: Math.floor(elapsed),
    phase_milli: Math.floor((beats % quantum) * 1000),
    quantum,
    tempo_valid: true,
    hostname: "neon-link.local",
    ip: setupAp ? "192.168.4.1" : "10.0.0.42",
    setup_ap: setupAp,
    wifi_ssid: config.wifi.networks[0].ssid,
    wifi_pass_len: 0,
    wifi_fail_reason: 0,
    firmware: "mock-1.0",
    device_name: config.device_name,
    ap_ssid: setupAp ? "NEON-LINK-1234" : "",
    set_bpm: config.tempo_milli_bpm / 1000,
    pulse: { edges: Math.floor(beats * 4), late_max_us: 184, late_avg_us: 12 },
    audio: {
      running: config.audio.enabled,
      underruns: 0,
      // A meter that moves, so the page can be judged in motion.
      peak_l: Math.floor(400 + 300 * Math.abs(Math.sin(elapsed * 2))),
      peak_r: Math.floor(400 + 300 * Math.abs(Math.sin(elapsed * 2 + 0.4))),
      publishing: config.audio.publish_mix,
      subscribers: config.audio.publish_mix ? 1 : 0,
      sub_state: config.audio.sub_channel_id ? "playing" : "idle",
      sub_rate: config.audio.sub_channel_id ? 48000 : 0,
      sub_dropped: 0,
      fill_ms: config.audio.sub_channel_id ? 210 : 0,
      clock_ppm: 43,
    },
  };
};

// Channels a Live 12.4 on the same network would be offering, plus our own.
const audioChannels = () => ({
  available: true,
  channels: [
    {
      id: "8f2a01c4d8e0ab11",
      name: "Master",
      peer: "Live 12",
      rate: 48000,
      channels: 2,
      local: false,
    },
    {
      id: "8f2a01c4d8e0ab12",
      name: "Drums",
      peer: "Live 12",
      rate: 48000,
      channels: 2,
      local: false,
    },
    {
      id: "00aabbccddeeff01",
      name: `${config.audio.channel_name || config.device_name} Out`,
      peer: config.device_name,
      rate: 48000,
      channels: 2,
      local: true,
    },
  ],
});

const json = (res, body) => {
  res.writeHead(200, { "Content-Type": "application/json" });
  res.end(JSON.stringify(body));
};

createServer((req, res) => {
  const url = new URL(req.url ?? "/", "http://localhost");

  if (url.pathname === "/api/status") return json(res, status());
  if (url.pathname === "/api/config" && req.method === "GET") return json(res, config);
  if (url.pathname === "/api/config" && req.method === "PUT") {
    let body = "";
    req.on("data", (c) => (body += c));
    req.on("end", () => {
      const next = JSON.parse(body);
      next.wifi.networks = next.wifi.networks.map((n) => ({
        ...n,
        has_pass: n.pass !== "" || n.has_pass === true,
        pass: "",
      }));
      next.wifi.pass = "";
      next.ap = { ...next.ap, has_pass: next.ap.pass !== "" || next.ap.has_pass === true, pass: "" };
      config = next;
      json(res, config);
    });
    return;
  }
  if (url.pathname === "/api/audio/channels") return json(res, audioChannels());
  if (url.pathname === "/api/scan") {
    return json(res, [
      { ssid: "Studio 2.4", rssi: -42, open: false },
      { ssid: "Green Room", rssi: -67, open: false },
      { ssid: "Venue Guest", rssi: -78, open: true },
    ]);
  }
  if (url.pathname === "/api/transport") {
    const op = url.searchParams.get("op");
    if (op === "toggle") playing = !playing;
    else if (op === "play" || op === "play_now") playing = true;
    else if (op === "stop" || op === "stop_now") playing = false;
    return json(res, { ok: true });
  }
  if (
    url.pathname === "/api/preset" ||
    url.pathname === "/api/reboot" ||
    url.pathname === "/api/tempo" ||
    url.pathname === "/api/resync" ||
    url.pathname === "/api/factory_reset" ||
    url.pathname === "/api/ota"
  ) {
    return json(res, { ok: true });
  }

  if (!existsSync(bundle)) {
    res.writeHead(404, { "Content-Type": "text/plain" });
    return res.end("Run `npm run build` first.");
  }
  res.writeHead(200, { "Content-Type": "text/html" });
  res.end(readFileSync(bundle));
}).listen(port, () => {
  console.log(`mock module on http://localhost:${port}/  (setup_ap=${setupAp})`);
});
