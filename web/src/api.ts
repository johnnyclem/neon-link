/**
 * Typed wrappers over the module's HTTP API.
 *
 * The endpoints are exactly the ones the firmware already serves
 * (components/web_ui/src/web_ui.cpp) — this layer adds types and error
 * handling, it does not add surface area.
 */

export type PulseMode = "trig" | "square";
export type Rhythm = "all" | "euclid" | "probability" | "pattern";
export type ResetMode = "start" | "bar" | "off" | "stop";
export type OutputRole =
  | "clock"
  | "gate"
  | "reset_loop"
  | "reset_start"
  | "reset_stop";
export type ApPolicy = "fallback" | "always" | "off";
export type ClockSource = "auto" | "link" | "external";
export type ClockPolicy = "ignore" | "replace" | "merge";
export type NetworkKind = "ethernet" | "wifi" | "none";
export type AudioRole =
  | "mix"
  | "metronome"
  | "clock"
  | "reset"
  | "run"
  | "synth"
  | "link_in"
  | "line_in";
export type ClickSound = "sine" | "noise" | "wood";
export type SubState = "idle" | "buffering" | "playing";

export interface ClockConfig {
  enabled: boolean;
  ppqn: number;
  mult: number;
  div: number;
  mode: PulseMode;
  trig_len_us: number;
  duty_pct: number;
  shuffle_pct: number;
  rhythm: Rhythm;
  euclid_steps: number;
  euclid_fills: number;
  euclid_rot: number;
  probability_pct: number;
  humanize_pct: number;
  /** What the jack does: clock, gate, or one of the reset flavours. */
  role: OutputRole;
  /** "Clock (Always On)" — keeps pulsing through a transport stop. */
  free_run: boolean;
  /** 64-step free-assignment mask, 16 hex chars (JSON numbers lose bits). */
  step_mask: string;
  /** Spread the pattern's steps over one loop instead of the PPQN grid. */
  rhythm_over_loop: boolean;
}

export interface WifiNetwork {
  ssid: string;
  pass: string;
  /** Read-only: whether a password is stored (never the password itself). */
  has_pass?: boolean;
  hidden: boolean;
}

export interface ScanResult {
  ssid: string;
  rssi: number;
  open: boolean;
}

export interface AudioConfig {
  /** Master switch. Takes effect on the next boot: it starts the I2S task. */
  enabled: boolean;
  /** What each output jack carries — the mix, or a solo tap of one source. */
  role_l: AudioRole;
  role_r: AudioRole;
  metro_enabled: boolean;
  metro_sound: ClickSound;
  /** 0..255, where 200 is unity. */
  metro_gain: number;
  metro_accent: boolean;
  amy_enabled: boolean;
  amy_gain: number;
  amy_patch: number;
  /** 0 leaves the line input unmonitored. */
  linein_monitor_gain: number;
  /** Publish the master mix as a Link Audio channel. */
  publish_mix: boolean;
  /** Publish the line input as a second channel. */
  publish_linein: boolean;
  /** Halve the bitrate on a busy network. */
  publish_mono: boolean;
  /** 120 Hz–5 kHz band on subscribe. Off = full band. */
  gist_lpf: boolean;
  sub_gain: number;
  /** Receive buffer depth: latency traded against WiFi jitter. */
  jitter_ms: number;
  /** Empty derives the published names from the device name. */
  channel_name: string;
  /** Empty means not subscribed. */
  sub_channel_id: string;
}

export interface AudioChannel {
  id: string;
  name: string;
  /** Publishing peer's display name. */
  peer?: string;
  rate: number;
  channels: number;
  /** One of ours — subscribing to it would be a loop. */
  local: boolean;
}

export interface AudioStatus {
  running: boolean;
  underruns: number;
  /** Output peak, 0..1000 milli-full-scale. */
  peak_l: number;
  peak_r: number;
  /** At least one peer is listening to something we publish. */
  publishing: boolean;
  subscribers: number;
  sub_state: SubState;
  /** Sender rate of the subscribed channel, 0 when not receiving. */
  sub_rate: number;
  sub_dropped: number;
  /** Receive-buffer fill, milliseconds. */
  fill_ms?: number;
  /** Sample-clock drift against the module's own timebase. */
  clock_ppm: number;
  /** SampleClock residual, microseconds. Walks if G3 resync fails. */
  clock_residual_us?: number;
}

export interface Config {
  engine: {
    clocks: ClockConfig[];
    reset_mode: ResetMode;
    reset_trig_len_us: number;
    latency_us: number;
    transport_gating: boolean;
    /** Let the loop reset lead the clock edge it belongs to. */
    reset_before_edge: boolean;
    reset_lead_us: number;
  };
  tempo_cv: { min_bpm: number; max_bpm: number };
  quantum: number;
  clock_source: ClockSource;
  clock_in_ppqn: number;
  ble: {
    enabled: boolean;
    midi_clock_out: boolean;
    channel: number;
    gate_target: number;
    pitch_cv: boolean;
    cc_latency: number;
    cc_shuffle_base: number;
    clock_policy: ClockPolicy;
    transport_enabled: boolean;
  };
  /** Follow (and broadcast) transport changes from other Link peers. */
  start_stop_sync: boolean;
  /** MIDI-only offset, independent of the CV delay in engine.latency_us. */
  midi_nudge_us: number;
  /** Drives <name>.local and the default access point SSID. */
  device_name: string;
  display_brightness: number;
  /** Full-screen 1/2/3/4 on the panel while the transport is running. */
  big_beat_display: boolean;
  audio: AudioConfig;
  tempo_milli_bpm: number;
  wifi: { networks: WifiNetwork[]; retries: number; ssid: string; pass: string };
  ap: {
    policy: ApPolicy;
    ssid: string;
    pass: string;
    has_pass?: boolean;
    require_pass: boolean;
    hidden: boolean;
    channel: number;
  };
  /**
   * Per-device secret generated at first boot, required (as the
   * X-Neon-Token header) on /api/ota and /api/factory_reset. Read-only:
   * the firmware never accepts a new value for it over PUT /api/config.
   */
  device_token: string;
}

export interface Status {
  bpm: number;
  peers: number;
  playing: boolean;
  network: NetworkKind;
  ext_clock: boolean;
  uptime_s: number;
  /** Position within the bar, milli-beats 0..quantum*1000. */
  phase_milli: number;
  quantum: number;
  /** False until the first Link sync, so the tempo can show its placeholder. */
  tempo_valid: boolean;
  hostname: string;
  ip: string;
  setup_ap: boolean;
  wifi_ssid: string;
  wifi_pass_len: number;
  wifi_fail_reason: number;
  /** Running firmware version, for the update card. */
  firmware: string;
  /**
   * Config generation. Increments on apply/save. Absent on firmware
   * that predates the plugin work — treat as 0.
   */
  rev?: number;
  device_name: string;
  /** SSID the access point is advertising, "" when it is down. */
  ap_ssid: string;
  /** The tempo the module holds when no peer is dictating one. */
  set_bpm: number;
  pulse: { edges: number; late_max_us: number; late_avg_us: number };
  audio: AudioStatus;
}

async function json<T>(input: string, init?: RequestInit): Promise<T> {
  const ctrl = new AbortController();
  const timer = setTimeout(() => ctrl.abort(), 10000);
  try {
    const res = await fetch(input, {
      ...init,
      cache: "no-store",
      signal: init?.signal ?? ctrl.signal,
    });
    if (!res.ok) {
      throw new Error(`${init?.method ?? "GET"} ${input} → ${res.status}`);
    }
    return (await res.json()) as T;
  } finally {
    clearTimeout(timer);
  }
}

/**
 * Fold a device reply onto the config we already have. A partial or
 * unexpected body (or a reply that arrived after a WiFi bounce) must not
 * wipe the form the user just saved.
 */
export function adoptConfig(applied: Partial<Config>, fallback: Config): Config {
  const wifi = applied.wifi ?? fallback.wifi;
  const ap = applied.ap ?? fallback.ap;
  return {
    ...fallback,
    ...applied,
    engine: {
      ...fallback.engine,
      ...applied.engine,
      clocks: applied.engine?.clocks ?? fallback.engine.clocks,
    },
    audio: { ...fallback.audio, ...applied.audio },
    ble: { ...fallback.ble, ...applied.ble },
    tempo_cv: { ...fallback.tempo_cv, ...applied.tempo_cv },
    wifi: { ...wifi, pass: "" },
    ap: { ...ap, pass: "" },
  };
}

export const api = {
  getConfig: () => json<Config>("/api/config"),

  /** The device replies with the sanitized result, which we adopt verbatim. */
  putConfig: (cfg: Config) =>
    json<Config>("/api/config", {
      method: "PUT",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(cfg),
    }),

  getStatus: () => json<Status>("/api/status"),

  preset: (op: "save" | "recall", slot: number) =>
    json<{ ok: boolean }>(`/api/preset?op=${op}&slot=${slot}`, {
      method: "POST",
    }),

  /** Play/stop land on the next loop boundary unless the op says "_now". */
  transport: (op: "play" | "stop" | "toggle" | "play_now" | "stop_now") =>
    json<{ ok: boolean }>(`/api/transport?op=${op}`, { method: "POST" }),

  setTempo: (bpm: number) =>
    json<{ ok: boolean }>(`/api/tempo?bpm=${bpm}`, { method: "POST" }),

  tempoOp: (op: "tap" | "double" | "half" | "nudge", delta?: number) =>
    json<{ ok: boolean }>(
      `/api/tempo?op=${op}${delta !== undefined ? `&delta=${delta}` : ""}`,
      { method: "POST" },
    ),

  /** Reset on the next loop, or re-align the Link grid to this instant. */
  resync: (op: "next" | "now") =>
    json<{ ok: boolean }>(`/api/resync?op=${op}`, { method: "POST" }),

  /** Blocking on the device — a full scan takes a couple of seconds. */
  scan: () => json<ScanResult[]>("/api/scan"),

  /**
   * Link Audio channels visible on the session right now, ours included.
   * `available` is false when the firmware has no Link Audio behind it, so
   * the page can say that rather than showing an empty list.
   */
  audioChannels: () =>
    json<{ available: boolean; channels: AudioChannel[] }>(
      "/api/audio/channels",
    ),

  /**
   * `token` is `cfg.device_token`: both this and ota() below gate on it
   * (a custom header a cross-site request cannot attach), on top of the
   * Host/Origin check every other mutating endpoint already gets.
   */
  factoryReset: async (token: string) => {
    try {
      await fetch("/api/factory_reset?confirm=yes", {
        method: "POST",
        headers: { "X-Neon-Token": token },
      });
    } catch {
      /* the module reboots into defaults, which is the point */
    }
  },

  /** Streams a firmware image into the inactive slot; the module reboots. */
  ota: async (image: File, token: string): Promise<void> => {
    const res = await fetch("/api/ota", {
      method: "POST",
      headers: { "X-Neon-Token": token },
      body: image,
    });
    if (!res.ok) {
      throw new Error(`update failed (${res.status})`);
    }
  },

  /** The connection drops mid-reboot; a rejected fetch here is expected. */
  reboot: async () => {
    try {
      await fetch("/api/reboot", { method: "POST" });
    } catch {
      /* the module went down, which is the point */
    }
  },
};

/** Toggle one step in a 64-bit mask carried as 16 hex characters. */
export function toggleStep(mask: string, step: number): string {
  const bits = BigInt(`0x${mask || "0"}`) ^ (1n << BigInt(step));
  return bits.toString(16).padStart(16, "0");
}

export function stepIsOn(mask: string, step: number): boolean {
  return ((BigInt(`0x${mask || "0"}`) >> BigInt(step)) & 1n) === 1n;
}

/**
 * Turns an ESP-IDF disconnect reason into something actionable. The raw
 * number is useless to the person holding the module; "wrong password" is
 * not (DESIGN_SYSTEM.md §9: state the problem and the next action).
 */
export function wifiFailHint(reason: number): string {
  switch (reason) {
    case 0:
      return "";
    case 15:
      return "Handshake timed out — usually a wrong password.";
    case 201:
      return "Network not found — check the spelling, and that it is 2.4 GHz.";
    case 2:
    case 202:
    case 204:
      return "Authentication failed — check the password and the WPA mode.";
    default:
      return `Disconnected (reason ${reason}).`;
  }
}
