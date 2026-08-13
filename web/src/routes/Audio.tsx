import { useCallback, useEffect, useState } from "preact/hooks";
import type { PageProps } from "../app";
import { api, type AudioChannel, type AudioRole } from "../api";
import { strings } from "../design/strings";
import {
  Button,
  Card,
  NumberField,
  SelectField,
  TextField,
  Toggle,
} from "../components/controls";
import { SubNav } from "../components/SubNav";
import { SaveBar } from "./SaveBar";

const ROLES: { value: AudioRole; label: string }[] = [
  { value: "mix", label: "Mix — everything enabled below" },
  { value: "metronome", label: "Metronome only" },
  { value: "clock", label: "Clock (CLK 1, as audio)" },
  { value: "reset", label: "Reset (as audio)" },
  { value: "run", label: "Run gate (as audio)" },
  { value: "synth", label: "Synth only" },
  { value: "link_in", label: "Link Audio in" },
  { value: "line_in", label: "Line in" },
];

/** Gains travel as 0..255 with 200 = unity; people think in percent. */
const UNITY = 200;
const toPct = (v: number) => Math.round((v * 100) / UNITY);
const fromPct = (p: number) => Math.min(255, Math.round((p * UNITY) / 100));

function Meter({ label, value }: { label: string; value: number }) {
  const pct = Math.min(100, value / 10);
  return (
    <div class="meter">
      <span class="meter__label mono">{label}</span>
      <span class="meter__track">
        <span class="meter__fill" style={`width:${pct}%`} />
      </span>
    </div>
  );
}

/** The audio path: what comes out of the jacks, and what goes over WiFi. */
export function Audio(props: PageProps) {
  const { cfg, patch, status } = props;
  const a = cfg.audio;
  const s = status?.audio;
  const [pane, setPane] = useState<"out" | "stream">("out");
  const [channels, setChannels] = useState<AudioChannel[] | null>(null);
  const [available, setAvailable] = useState(true);
  const [scanning, setScanning] = useState(false);

  const refresh = useCallback(async () => {
    setScanning(true);
    try {
      const res = await api.audioChannels();
      setAvailable(res.available);
      setChannels(res.channels);
    } catch {
      setChannels([]);
    } finally {
      setScanning(false);
    }
  }, []);

  useEffect(() => {
    if (pane === "stream") void refresh();
  }, [pane, refresh]);

  return (
    <>
      <h1 class="page-title">{strings.screens.audio.web}</h1>

      {a.enabled && s && !s.running ? (
        <div class="banner">
          <div class="banner__body">
            <strong class="banner__title">Audio is not running</strong>
            The engine starts at boot, and only when the I2S pins are set for
            this board. Save, reboot, and check the console if it stays quiet.
          </div>
        </div>
      ) : null}

      <SubNav
        label="Audio section"
        value={pane}
        onChange={(id) => setPane(id as "out" | "stream")}
        items={[
          { id: "out", label: "Out" },
          { id: "stream", label: "Stream" },
        ]}
      />

      {pane === "out" ? (
        <>
          <Card
            title="Output"
            note="A jack set to a clock, reset or run role carries that pulse as audio — one sample of placement instead of a millisecond of I²C."
          >
            <Toggle
              label="Audio engine enabled (takes effect on reboot)"
              checked={a.enabled}
              onChange={(v) => patch((d) => (d.audio.enabled = v))}
            />
            <div class="fields" style="margin-top:var(--space-3)">
              <SelectField
                label="Left carries"
                value={a.role_l}
                options={ROLES}
                onChange={(v) => patch((d) => (d.audio.role_l = v))}
              />
              <SelectField
                label="Right carries"
                value={a.role_r}
                options={ROLES}
                onChange={(v) => patch((d) => (d.audio.role_r = v))}
              />
            </div>
            {s ? (
              <div style="margin-top:var(--space-3)">
                <Meter label="L" value={s.peak_l} />
                <Meter label="R" value={s.peak_r} />
              </div>
            ) : null}
          </Card>

          <Card title="Metronome">
            <Toggle
              label="Click on every beat"
              checked={a.metro_enabled}
              onChange={(v) => patch((d) => (d.audio.metro_enabled = v))}
            />
            <div class="fields" style="margin-top:var(--space-3)">
              <SelectField
                label="Sound"
                value={a.metro_sound}
                options={[
                  { value: "sine", label: "Sine" },
                  { value: "noise", label: "Noise" },
                  { value: "wood", label: "Wood" },
                ]}
                onChange={(v) => patch((d) => (d.audio.metro_sound = v))}
              />
              <NumberField
                label="Level %"
                value={toPct(a.metro_gain)}
                min={0}
                max={127}
                onChange={(v) => patch((d) => (d.audio.metro_gain = fromPct(v)))}
              />
            </div>
            <div style="margin-top:var(--space-3)">
              <Toggle
                label="Accent the downbeat"
                checked={a.metro_accent}
                onChange={(v) => patch((d) => (d.audio.metro_accent = v))}
              />
            </div>
          </Card>

          <Card
            title="Synth"
            note="Notes arriving over BLE or TRS MIDI, played on the Link timeline."
          >
            <Toggle
              label="Synth voice enabled"
              checked={a.amy_enabled}
              onChange={(v) => patch((d) => (d.audio.amy_enabled = v))}
            />
            <div class="fields" style="margin-top:var(--space-3)">
              <NumberField
                label="Patch"
                value={a.amy_patch}
                min={0}
                max={3}
                onChange={(v) => patch((d) => (d.audio.amy_patch = v))}
              />
              <NumberField
                label="Level %"
                value={toPct(a.amy_gain)}
                min={0}
                max={127}
                onChange={(v) => patch((d) => (d.audio.amy_gain = fromPct(v)))}
              />
            </div>
          </Card>

          <Card
            title="Line in"
            note="0 % leaves the input unmonitored — it can still be published without being audible here."
          >
            <div class="fields">
              <NumberField
                label="Monitor %"
                value={toPct(a.linein_monitor_gain)}
                min={0}
                max={127}
                onChange={(v) =>
                  patch((d) => (d.audio.linein_monitor_gain = fromPct(v)))
                }
              />
            </div>
          </Card>
        </>
      ) : (
        <>
          {!available ? (
            <div class="banner">
              <div class="banner__body">
                <strong class="banner__title">Streaming is not in this firmware</strong>
                Link Audio needs an Ableton Link 4.0 build. Everything else on
                this page works; publishing and subscribing do nothing.
              </div>
            </div>
          ) : null}

          <Card
            title="Publish"
            note={
              s
                ? `${s.subscribers} peer${s.subscribers === 1 ? "" : "s"} listening. Link only transmits while someone is subscribed.`
                : "Link only transmits while a peer is subscribed."
            }
          >
            <div style="display:flex;flex-direction:column;gap:var(--space-1)">
              <Toggle
                label="Publish the mix"
                checked={a.publish_mix}
                onChange={(v) => patch((d) => (d.audio.publish_mix = v))}
              />
              <Toggle
                label="Publish the line input"
                checked={a.publish_linein}
                onChange={(v) => patch((d) => (d.audio.publish_linein = v))}
              />
              <Toggle
                label="Mono (halves the bitrate)"
                checked={a.publish_mono}
                onChange={(v) => patch((d) => (d.audio.publish_mono = v))}
              />
            </div>
            <div class="fields" style="margin-top:var(--space-3)">
              <TextField
                label="Channel name"
                value={a.channel_name}
                placeholder={cfg.device_name}
                maxLength={23}
                onChange={(v) => patch((d) => (d.audio.channel_name = v))}
              />
            </div>
            <p class="card__note">
              Published as “{(a.channel_name || cfg.device_name) + " Out"}”
              {a.publish_linein
                ? ` and “${(a.channel_name || cfg.device_name) + " In"}”`
                : ""}
              .
            </p>
          </Card>

          <Card
            title="Subscribe"
            actions={
              <Button variant="secondary" onClick={() => void refresh()}>
                {scanning ? "Looking…" : "Refresh"}
              </Button>
            }
            note="A deeper buffer survives a busier network at the cost of latency."
          >
            <div class="fields">
              <SelectField
                label="Channel"
                value={a.sub_channel_id}
                options={[
                  { value: "", label: "Not subscribed" },
                  ...(channels ?? [])
                    .filter((c) => !c.local)
                    .map((c) => ({
                      value: c.id,
                      label: c.peer ? `${c.peer} / ${c.name}` : c.name,
                    })),
                  ...(a.sub_channel_id &&
                  !(channels ?? []).some((c) => c.id === a.sub_channel_id)
                    ? [{ value: a.sub_channel_id, label: `${a.sub_channel_id} (offline)` }]
                    : []),
                ]}
                onChange={(v) => patch((d) => (d.audio.sub_channel_id = v))}
              />
              <NumberField
                label="Buffer ms"
                value={a.jitter_ms}
                min={5}
                max={800}
                step={5}
                onChange={(v) => patch((d) => (d.audio.jitter_ms = v))}
              />
              <NumberField
                label="Level %"
                value={toPct(a.sub_gain)}
                min={0}
                max={127}
                onChange={(v) => patch((d) => (d.audio.sub_gain = fromPct(v)))}
              />
            </div>
            {s && a.sub_channel_id ? (
              <p class="card__note">
                {s.sub_state === "playing"
                  ? `Playing at ${Math.round(s.sub_rate / 100) / 10} kHz`
                  : s.sub_state === "buffering"
                    ? "Buffering…"
                    : "Waiting for audio"}
                {s.fill_ms != null ? ` · fill ${s.fill_ms} ms` : ""}
                {s.sub_dropped > 0 ? ` · ${s.sub_dropped} dropped` : ""}
              </p>
            ) : null}
            <p class="card__note">
              Set the left or right jack to “Link Audio in” to hear it.
            </p>
          </Card>
        </>
      )}

      <SaveBar {...props} />
    </>
  );
}
