import { useState } from "preact/hooks";
import type { PageProps } from "../app";
import { api } from "../api";
import { strings } from "../design/strings";
import { HeroTempo } from "../components/HeroTempo";
import { PhaseBar } from "../components/PhaseBar";
import { StatusChip } from "../components/StatusChip";
import { networkState } from "../components/StatusStrip";
import { BeatStage, beatFromStatus } from "../components/BeatStage";
import { SubNav } from "../components/SubNav";
import { Button, Card, Readout } from "../components/controls";

/**
 * The expanded live screen — a one-handed remote when the module is in
 * a case, and the same five facts the panel shows when you have a desk.
 */
export function Live({ status, cfg }: PageProps) {
  const [presetMsg, setPresetMsg] = useState("");
  const [bpmDraft, setBpmDraft] = useState("");
  const [pane, setPane] = useState<"sync" | "set" | "stats">("sync");

  // Transport and tempo are commands, not settings: they take effect at
  // once (play/stop on the next loop boundary) and never wait for a save.
  const send = (run: () => Promise<unknown>) => {
    void run().catch(() => setPresetMsg("Command failed."));
  };

  const preset = async (op: "save" | "recall", slot: number) => {
    try {
      await api.preset(op, slot);
      setPresetMsg(`Slot ${slot + 1} ${op === "save" ? "saved" : "recalled"}.`);
    } catch {
      setPresetMsg(`Slot ${slot + 1} ${op} failed.`);
    }
  };

  const quantum = status?.quantum ?? 4;
  const phase = status ? status.phase_milli / (quantum * 1000) : 0;
  const beat = status ? beatFromStatus(status.phase_milli, quantum) : 1;
  const playing = status?.playing === true;
  const showBeat = playing && cfg.big_beat_display !== false;

  return (
    <>
      <h1 class="page-title page-title--live">{strings.screens.live.web}</h1>

      <Card title={showBeat ? `Beat ${beat}` : "Tempo"}>
        {showBeat ? (
          <BeatStage beat={beat} playing />
        ) : (
          <HeroTempo bpm={status && status.tempo_valid ? status.bpm : null} size={72} />
        )}
        {showBeat && status?.tempo_valid ? (
          <div class="beat-stage__bpm">
            <HeroTempo bpm={status.bpm} size={28} />
          </div>
        ) : null}

        <div class="transport">
          <Button
            variant={playing ? "secondary" : "primary"}
            onClick={() => send(() => api.transport("toggle"))}
          >
            {playing ? "Stop" : "Play"}
          </Button>
          <Button variant="secondary" onClick={() => send(() => api.tempoOp("tap"))}>
            Tap
          </Button>
          <Button
            variant="secondary"
            onClick={() => send(() => api.tempoOp("nudge", -1))}
          >
            −1
          </Button>
          <Button
            variant="secondary"
            onClick={() => send(() => api.tempoOp("nudge", 1))}
          >
            +1
          </Button>
          <Button variant="secondary" onClick={() => send(() => api.tempoOp("half"))}>
            ÷2
          </Button>
          <Button variant="secondary" onClick={() => send(() => api.tempoOp("double"))}>
            ×2
          </Button>
        </div>

        <div class="btn-row">
          <input
            type="number"
            min={20}
            max={999}
            step={0.5}
            inputMode="decimal"
            class="tempo-input"
            aria-label="Set tempo"
            placeholder={status ? status.set_bpm.toFixed(1) : "120.0"}
            value={bpmDraft}
            onInput={(e) => setBpmDraft((e.target as HTMLInputElement).value)}
          />
          <Button
            variant="secondary"
            disabled={bpmDraft === ""}
            onClick={() => {
              const bpm = Number(bpmDraft);
              if (!Number.isNaN(bpm)) {
                send(() => api.setTempo(bpm));
                setBpmDraft("");
              }
            }}
          >
            Set BPM
          </Button>
        </div>

        <div class="live-phase">
          <PhaseBar
            phase={phase}
            quantum={quantum}
            running={playing}
            height={44}
          />
        </div>
        <div class="strip__chips" style="margin:var(--space-3) 0 0">
          {status ? (
            <>
              <StatusChip state={status.ext_clock ? "source_ext" : "source_link"} />
              <StatusChip state={playing ? "transport_run" : "transport_stop"} />
              <StatusChip state={networkState(status)} detail={`${status.peers}P`} />
            </>
          ) : null}
        </div>
      </Card>

      <SubNav
        label="Live extras"
        value={pane}
        onChange={(id) => setPane(id as typeof pane)}
        items={[
          { id: "sync", label: "Sync" },
          { id: "set", label: "Set" },
          { id: "stats", label: "Stats" },
        ]}
      />

      {pane === "sync" ? (
      <Card title="Resync">
        <div class="btn-row" style="margin-top:0">
          <Button variant="secondary" onClick={() => send(() => api.resync("next"))}>
            Reset next loop
          </Button>
          <Button variant="secondary" onClick={() => send(() => api.resync("now"))}>
            Re-align grid now
          </Button>
        </div>
      </Card>
      ) : null}

      {status && pane === "sync" && status.tempo_valid && status.peers === 0 && !status.setup_ap ? (
        <div class="banner">
          <div class="banner__body">
            <strong class="banner__title">{strings.states.no_link.web}</strong>
            {strings.states.no_link.long}
          </div>
        </div>
      ) : null}

      {pane === "set" ? (
      <Card title="Presets">
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
      ) : null}

      {pane === "stats" ? (
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
      ) : null}
    </>
  );
}

function formatUptime(seconds: number): string {
  const h = Math.floor(seconds / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  const s = seconds % 60;
  return h > 0 ? `${h}h ${m}m` : m > 0 ? `${m}m ${s}s` : `${s}s`;
}
