/**
 * Typed wrappers over the module's HTTP API.
 *
 * The endpoints are exactly the ones the firmware already serves
 * (components/web_ui/src/web_ui.cpp) — this layer adds types and error
 * handling, it does not add surface area.
 */

export type PulseMode = "trig" | "square";
export type Rhythm = "all" | "euclid" | "probability";
export type ResetMode = "start" | "bar" | "off";
export type ClockSource = "auto" | "link" | "external";
export type ClockPolicy = "ignore" | "replace" | "merge";
export type NetworkKind = "ethernet" | "wifi" | "none";

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
}

export interface Config {
  engine: {
    clocks: ClockConfig[];
    reset_mode: ResetMode;
    reset_trig_len_us: number;
    latency_us: number;
    transport_gating: boolean;
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
  wifi: { ssid: string; pass: string };
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
  pulse: { edges: number; late_max_us: number; late_avg_us: number };
}

async function json<T>(input: string, init?: RequestInit): Promise<T> {
  const res = await fetch(input, init);
  if (!res.ok) {
    throw new Error(`${init?.method ?? "GET"} ${input} → ${res.status}`);
  }
  return (await res.json()) as T;
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

  /** The connection drops mid-reboot; a rejected fetch here is expected. */
  reboot: async () => {
    try {
      await fetch("/api/reboot", { method: "POST" });
    } catch {
      /* the module went down, which is the point */
    }
  },
};

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
