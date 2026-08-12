import { useState } from "preact/hooks";
import type { PageProps } from "../app";
import { api } from "../api";
import { strings } from "../design/strings";
import { HeroTempo } from "../components/HeroTempo";
import { PhaseBar } from "../components/PhaseBar";
import { StatusChip } from "../components/StatusChip";
import { networkState } from "../components/StatusStrip";
import { Button, Card, Readout } from "../components/controls";

/**
 * The expanded live screen — the same five facts the panel shows, with room
 * to breathe and the things a panel cannot offer (presets, reachability).
 */
export function Live({ status }: PageProps) {
  const [presetMsg, setPresetMsg] = useState("");

  const preset = async (op: "save" | "recall", slot: number) => {
    try {
      await api.preset(op, slot);
      setPresetMsg(`Slot ${slot + 1} ${op === "save" ? "saved" : "recalled"}.`);
    } catch {
      setPresetMsg(`Slot ${slot + 1} ${op} failed.`);
    }
  };

  const phase = status ? status.phase_milli / (status.quantum * 1000) : 0;

  return (
    <>
      <h1 class="page-title">{strings.screens.live.web}</h1>

      <Card title="Tempo">
        <HeroTempo bpm={status && status.tempo_valid ? status.bpm : null} size={72} />
        <div style="margin-top:var(--space-3)">
          <PhaseBar
            phase={phase}
            quantum={status?.quantum ?? 4}
            running={status?.playing ?? false}
            height={44}
          />
        </div>
        <div class="strip__chips" style="margin:var(--space-3) 0 0">
          {status ? (
            <>
              <StatusChip state={status.ext_clock ? "source_ext" : "source_link"} />
              <StatusChip state={status.playing ? "transport_run" : "transport_stop"} />
              <StatusChip state={networkState(status)} detail={`${status.peers}P`} />
            </>
          ) : null}
        </div>
      </Card>

      {status && status.tempo_valid && status.peers === 0 && !status.setup_ap ? (
        <div class="banner">
          <div class="banner__body">
            <strong class="banner__title">{strings.states.no_link.web}</strong>
            {strings.states.no_link.long}
          </div>
        </div>
      ) : null}

      <Card
        title="Presets"
        note="Slots also recall from MIDI Program Change 1–4, so a set can switch the module without a phone in hand."
      >
        <div class="preset-grid">
          {[0, 1, 2, 3].map((slot) => (
            <div key={slot} class="btn-row" style="margin-top:0">
              <span class="field__label" style="min-width:4em">
                Slot {slot + 1}
              </span>
              <Button variant="secondary" onClick={() => void preset("save", slot)}>
                Save
              </Button>
              <Button variant="secondary" onClick={() => void preset("recall", slot)}>
                Recall
              </Button>
            </div>
          ))}
        </div>
        {presetMsg ? (
          <p class="btn-row__msg" style="margin-top:var(--space-2)">
            {presetMsg}
          </p>
        ) : null}
      </Card>

      <Card title="Timing">
        <Readout label="Edges emitted" value={<span class="mono">{status?.pulse.edges ?? "—"}</span>} />
        <Readout
          label="Worst lateness"
          value={<span class="mono">{status ? `${status.pulse.late_max_us} µs` : "—"}</span>}
        />
        <Readout
          label="Average lateness"
          value={<span class="mono">{status ? `${status.pulse.late_avg_us} µs` : "—"}</span>}
        />
        <Readout
          label="Uptime"
          value={<span class="mono">{status ? formatUptime(status.uptime_s) : "—"}</span>}
        />
      </Card>
    </>
  );
}

function formatUptime(seconds: number): string {
  const h = Math.floor(seconds / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  const s = seconds % 60;
  return h > 0 ? `${h}h ${m}m` : m > 0 ? `${m}m ${s}s` : `${s}s`;
}
