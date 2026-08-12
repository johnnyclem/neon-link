import { useState } from "preact/hooks";
import type { PageProps } from "../app";
import { strings } from "../design/strings";
import { StatusChip } from "../components/StatusChip";
import { Card, NumberField, SelectField, Toggle } from "../components/controls";
import { SubNav } from "../components/SubNav";
import { SaveBar } from "./SaveBar";

const OMNI = 255;
const TARGET_NONE = 255;
const CC_OFF = 255;

const CHANNELS = [
  { value: OMNI, label: "Omni (all channels)" },
  ...Array.from({ length: 16 }, (_, i) => ({ value: i, label: `Channel ${i + 1}` })),
];

const GATE_TARGETS = [
  { value: TARGET_NONE, label: "Not routed" },
  { value: 0, label: "CLK 1" },
  { value: 1, label: "CLK 2" },
  { value: 2, label: "CLK 3" },
  { value: 3, label: "CLK 4" },
  { value: 4, label: "RUN" },
];

/** BLE MIDI routing — the wireless half of the module's personality. */
export function Midi(props: PageProps) {
  const { cfg, patch } = props;
  const ble = cfg.ble;
  const [pane, setPane] = useState<"ble" | "route">("ble");

  return (
    <>
      <h1 class="page-title">{strings.screens.midi.web}</h1>

      <SubNav
        label="MIDI section"
        value={pane}
        onChange={(id) => setPane(id as "ble" | "route")}
        items={[
          { id: "ble", label: "BLE" },
          { id: "route", label: "Route" },
        ]}
      />

      {pane === "ble" ? (
      <Card title="Bluetooth MIDI">
        <div class="strip__chips" style="margin-bottom:var(--space-3)">
          <StatusChip state={ble.enabled ? "ble_on" : "ble_off"} />
        </div>
        <div style="display:flex;flex-direction:column;gap:var(--space-1)">
          <Toggle
            label="BLE MIDI enabled"
            checked={ble.enabled}
            onChange={(v) => patch((d) => (d.ble.enabled = v))}
          />
          <Toggle
            label="Send MIDI clock on TRS"
            checked={ble.midi_clock_out}
            onChange={(v) => patch((d) => (d.ble.midi_clock_out = v))}
          />
          <Toggle
            label="Follow transport messages"
            checked={ble.transport_enabled}
            onChange={(v) => patch((d) => (d.ble.transport_enabled = v))}
          />
        </div>
      </Card>
      ) : (
      <>
      <Card title="Routing">
        <div class="fields">
          <SelectField
            label="Listen on"
            value={ble.channel}
            options={CHANNELS}
            onChange={(v) => patch((d) => (d.ble.channel = v))}
          />
          <SelectField
            label="Notes drive"
            value={ble.gate_target}
            options={GATE_TARGETS}
            onChange={(v) => patch((d) => (d.ble.gate_target = v))}
            hint="Mono, last note wins"
          />
          <SelectField
            label="Incoming clock"
            value={ble.clock_policy}
            options={[
              { value: "ignore", label: "Ignore — Link is the timeline" },
              { value: "replace", label: "Replace the Link clock" },
              { value: "merge", label: "Merge both sources" },
            ]}
            onChange={(v) => patch((d) => (d.ble.clock_policy = v))}
          />
        </div>
        <div style="margin-top:var(--space-3)">
          <Toggle
            label="Note pitch to Tempo CV (1 V/oct)"
            checked={ble.pitch_cv}
            onChange={(v) => patch((d) => (d.ble.pitch_cv = v))}
          />
        </div>
      </Card>

      <Card
        title="Controller mapping"
        note={`Set a controller number to ${CC_OFF} to leave it unmapped. Shuffle uses four consecutive numbers from the base, one per clock output.`}
      >
        <div class="fields">
          <NumberField
            label="Latency CC"
            value={ble.cc_latency}
            min={0}
            max={255}
            onChange={(v) => patch((d) => (d.ble.cc_latency = v))}
          />
          <NumberField
            label="Shuffle base CC"
            value={ble.cc_shuffle_base}
            min={0}
            max={255}
            onChange={(v) => patch((d) => (d.ble.cc_shuffle_base = v))}
          />
        </div>
      </Card>
      </>
      )}

      <SaveBar {...props} />
    </>
  );
}
