import { useState } from "preact/hooks";
import type { PageProps } from "../app";
import { api } from "../api";
import { strings } from "../design/strings";
import {
  Button,
  Card,
  ConfirmModal,
  NumberField,
  Readout,
  SelectField,
  TextField,
  Toggle,
} from "../components/controls";
import { SaveBar } from "./SaveBar";
import { SubNav } from "../components/SubNav";

/** Timing, clock source, tempo CV range, and the destructive actions. */
export function System(props: PageProps) {
  const { cfg, status, patch } = props;
  const [confirming, setConfirming] = useState(false);
  const [rebooting, setRebooting] = useState(false);
  const [resetting, setResetting] = useState(false);
  const [image, setImage] = useState<File | null>(null);
  const [otaMsg, setOtaMsg] = useState("");
  const [pane, setPane] = useState<"time" | "clock" | "panel" | "mod">("time");

  const reboot = async () => {
    setConfirming(false);
    setRebooting(true);
    await api.reboot();
    window.setTimeout(() => setRebooting(false), 6000);
  };

  const factoryReset = async () => {
    setResetting(false);
    await api.factoryReset();
    setOtaMsg("");
  };

  const install = async () => {
    if (!image) return;
    setOtaMsg("Uploading…");
    try {
      await api.ota(image);
      setOtaMsg("Installed. The module is rebooting into the new image.");
    } catch (e) {
      setOtaMsg(e instanceof Error ? e.message : "Update failed.");
    }
  };

  return (
    <>
      <h1 class="page-title">{strings.screens.system.web}</h1>

      <SubNav
        label="System section"
        value={pane}
        onChange={(id) => setPane(id as typeof pane)}
        items={[
          { id: "time", label: "Time" },
          { id: "clock", label: "Clock" },
          { id: "panel", label: "Panel" },
          { id: "mod", label: "Module" },
        ]}
      />

      {pane === "time" ? (
      <Card title="Timing">
        <div class="fields">
          <NumberField
            label="Latency"
            value={cfg.engine.latency_us}
            min={-50000}
            max={50000}
            step={100}
            onChange={(v) => patch((d) => (d.engine.latency_us = v))}
            hint="µs"
          />
          <NumberField
            label="Quantum"
            value={cfg.quantum}
            min={1}
            max={16}
            onChange={(v) => patch((d) => (d.quantum = v))}
            hint="Beats per bar"
          />
          <SelectField
            label="Reset pulse"
            value={cfg.engine.reset_mode}
            options={[
              { value: "start", label: "On start of play" },
              { value: "bar", label: "Every bar" },
              { value: "stop", label: "On stop of play" },
              { value: "off", label: "Never" },
            ]}
            onChange={(v) => patch((d) => (d.engine.reset_mode = v))}
          />
          <NumberField
            label="Reset length"
            value={cfg.engine.reset_trig_len_us}
            min={100}
            max={100000}
            step={100}
            onChange={(v) => patch((d) => (d.engine.reset_trig_len_us = v))}
            hint="µs"
          />
          <NumberField
            label="MIDI nudge"
            value={cfg.midi_nudge_us}
            min={-100000}
            max={100000}
            step={500}
            onChange={(v) => patch((d) => (d.midi_nudge_us = v))}
            hint="µs — MIDI only, independent of the latency above"
          />
          <NumberField
            label="Reset lead"
            value={cfg.engine.reset_lead_us}
            min={0}
            max={50000}
            step={100}
            onChange={(v) => patch((d) => (d.engine.reset_lead_us = v))}
            hint="µs, when leading is on"
          />
        </div>
        <div style="margin-top:var(--space-3)">
          <Toggle
            label="Stop clock outputs when the transport stops"
            checked={cfg.engine.transport_gating}
            onChange={(v) => patch((d) => (d.engine.transport_gating = v))}
          />
          <Toggle
            label="Reset leads the clock edge"
            checked={cfg.engine.reset_before_edge}
            onChange={(v) => patch((d) => (d.engine.reset_before_edge = v))}
          />
          <Toggle
            label="Follow Link start/stop from other peers"
            checked={cfg.start_stop_sync}
            onChange={(v) => patch((d) => (d.start_stop_sync = v))}
          />
        </div>
      </Card>
      ) : null}

      {pane === "clock" ? (
      <>
      <Card title="Clock source">
        <div class="fields">
          <SelectField
            label="Source"
            value={cfg.clock_source}
            options={[
              { value: "auto", label: "Auto" },
              { value: "link", label: "Link is the master" },
              { value: "external", label: "External input is the master" },
            ]}
            onChange={(v) => patch((d) => (d.clock_source = v))}
          />
          <NumberField
            label="CLK IN rate"
            value={cfg.clock_in_ppqn}
            min={1}
            max={96}
            onChange={(v) => patch((d) => (d.clock_in_ppqn = v))}
            hint="Pulses per quarter note"
          />
        </div>
      </Card>

      <Card title="Tempo CV" note="The BPM range mapped across the 0–5 V output.">
        <div class="fields">
          <NumberField
            label="Minimum"
            value={cfg.tempo_cv.min_bpm}
            min={1}
            max={998}
            onChange={(v) => patch((d) => (d.tempo_cv.min_bpm = v))}
            hint="BPM at 0 V"
          />
          <NumberField
            label="Maximum"
            value={cfg.tempo_cv.max_bpm}
            min={2}
            max={999}
            onChange={(v) => patch((d) => (d.tempo_cv.max_bpm = v))}
            hint="BPM at 5 V"
          />
        </div>
      </Card>
      </>
      ) : null}

      {pane === "panel" ? (
      <Card title="Identity">
        <div class="fields">
          <TextField
            label="Device name"
            value={cfg.device_name}
            maxLength={23}
            onChange={(v) => patch((d) => (d.device_name = v))}
            hint={`http://${cfg.device_name || "neon-link"}.local/`}
          />
          <NumberField
            label="Display brightness"
            value={cfg.display_brightness}
            min={0}
            max={255}
            onChange={(v) => patch((d) => (d.display_brightness = v))}
            hint="0 blanks the panel"
          />
          <Toggle
            label="Big beat numbers"
            checked={cfg.big_beat_display !== false}
            onChange={(v) => patch((d) => (d.big_beat_display = v))}
          />
        </div>
      </Card>
      ) : null}

      {pane === "mod" ? (
      <>
      <Card title="Firmware">
        <Readout
          label="Installed"
          value={<span class="mono">{status?.firmware ?? "—"}</span>}
        />
        <div class="btn-row">
          <input
            type="file"
            accept=".bin"
            aria-label="Firmware image"
            onChange={(e) =>
              setImage((e.target as HTMLInputElement).files?.[0] ?? null)
            }
          />
          <Button variant="secondary" onClick={() => void install()} disabled={!image}>
            Install update
          </Button>
          {otaMsg ? <span class="btn-row__msg">{otaMsg}</span> : null}
        </div>
      </Card>

      <Card title="Module">
        <Readout label="Hostname" value={<span class="mono">{status?.hostname ?? "—"}</span>} />
        <Readout label="Address" value={<span class="mono">{status?.ip || "—"}</span>} />
        <div class="btn-row">
          <Button variant="danger" onClick={() => setConfirming(true)} disabled={rebooting}>
            {rebooting ? "Rebooting…" : "Reboot"}
          </Button>
          <Button variant="danger" onClick={() => setResetting(true)} disabled={rebooting}>
            Factory reset
          </Button>
          <span class="btn-row__msg">
            {rebooting
              ? "The module is restarting. Reload this page once it is back."
              : "The panel offers Reboot under SYSTEM, with its own confirmation."}
          </span>
        </div>
      </Card>
      </>
      ) : null}

      <SaveBar {...props} />

      {confirming ? (
        <ConfirmModal
          title="Reboot the module?"
          body="Clock outputs stop until it comes back up, and this page will lose its connection for a few seconds. Unsaved changes are not written."
          confirmLabel="Reboot"
          onConfirm={() => void reboot()}
          onCancel={() => setConfirming(false)}
        />
      ) : null}

      {resetting ? (
        <ConfirmModal
          title="Erase every setting?"
          body="Every setting and all four presets are erased, including the stored networks, and the module reboots into its setup access point. This cannot be undone."
          confirmLabel="Erase and reboot"
          onConfirm={() => void factoryReset()}
          onCancel={() => setResetting(false)}
        />
      ) : null}
    </>
  );
}
