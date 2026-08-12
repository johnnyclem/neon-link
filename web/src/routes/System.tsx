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
  Toggle,
} from "../components/controls";
import { SaveBar } from "./SaveBar";

/** Timing, clock source, tempo CV range, and the destructive actions. */
export function System(props: PageProps) {
  const { cfg, status, patch } = props;
  const [confirming, setConfirming] = useState(false);
  const [rebooting, setRebooting] = useState(false);

  const reboot = async () => {
    setConfirming(false);
    setRebooting(true);
    await api.reboot();
    window.setTimeout(() => setRebooting(false), 6000);
  };

  return (
    <>
      <h1 class="page-title">{strings.screens.system.web}</h1>

      <Card
        title="Timing"
        note="Latency compensation shifts every output in time. Positive values fire later; use it to line the module up against gear with its own input delay."
      >
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
        </div>
        <div style="margin-top:var(--space-3)">
          <Toggle
            label="Stop clock outputs when the transport stops"
            checked={cfg.engine.transport_gating}
            onChange={(v) => patch((d) => (d.engine.transport_gating = v))}
          />
        </div>
      </Card>

      <Card
        title="Clock source"
        note="Auto follows the external input whenever it is running and falls back to the Link session otherwise."
      >
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

      <Card title="Module">
        <Readout label="Hostname" value={<span class="mono">{status?.hostname ?? "—"}</span>} />
        <Readout label="Address" value={<span class="mono">{status?.ip || "—"}</span>} />
        <div class="btn-row">
          <Button variant="danger" onClick={() => setConfirming(true)} disabled={rebooting}>
            {rebooting ? "Rebooting…" : "Reboot"}
          </Button>
          <span class="btn-row__msg">
            {rebooting
              ? "The module is restarting. Reload this page once it is back."
              : "The panel offers the same action under SYSTEM, with its own confirmation."}
          </span>
        </div>
      </Card>

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
    </>
  );
}
