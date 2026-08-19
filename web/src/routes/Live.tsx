import { useState } from "preact/hooks";
import type { PageProps } from "../app";
import { api } from "../api";
import { strings } from "../design/strings";
import { HeroTempo } from "../components/HeroTempo";
import { PhaseBar } from "../components/PhaseBar";
import {
  BeatStage,
  BEAT_STYLES,
  beatStyleFrom,
  nextBeatStyle,
  type BeatStyle,
} from "../components/BeatStage";
import { SubNav } from "../components/SubNav";
import { Button, Card, Readout } from "../components/controls";
import { SaveBar } from "./SaveBar";

/**
 * The expanded live screen — a one-handed remote when the module is in
 * a case, and the same five facts the panel shows when you have a desk.
 */
export function Live(props: PageProps) {
  const { status, cfg, patch, save } = props;
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
  const playing = status?.playing === true;
  const showBeat = playing && cfg.big_beat_display !== false;
  const beatStyle = beatStyleFrom(cfg.beat_style);
  const setStyle = (style: BeatStyle) => {
    patch((d) => {
      d.beat_style = style;
    });
    void save();
  };

  return (
    <>
      <h1 class="page-title page-title--live">{strings.screens.live.web}</h1>

      <Card title={showBeat ? "Beat" : "Tempo"}>
        {showBeat ? (
          <BeatStage
            phaseMilli={status?.phase_milli ?? 0}
            quantum={quantum}
            bpm={status?.bpm ?? 120}
            playing
            style={beatStyle}
            onCycle={() => setStyle(nextBeatStyle(beatStyle))}
          />
        ) : (
          <HeroTempo bpm={status && status.tempo_valid ? status.bpm : null} size={72} />
        )}
        {showBeat ? (
          <div class="beat-stage__picker" role="radiogroup" aria-label="Beat style">
            {BEAT_STYLES.map((s) => (
              <button
                key={s.id}
                type="button"
                class="beat-stage__pick"
                role="radio"
                aria-checked={beatStyle === s.id}
                onClick={() => setStyle(s.id)}
              >
                {s.label}
              </button>
            ))}
          </div>
        ) : null}
        {showBeat && status?.tempo_valid ? (
          <div class="beat-stage__bpm">
            <HeroTempo bpm={status.bpm} size={28} />
          </div>
        ) : null}

        {/* The bar sits with the transport it describes: glance at Play,
            see where the loop is, hit Stop — one eye movement. */}
        <div class="live-phase">
          <PhaseBar phase={phase} quantum={quantum} running={playing} height={44} />
        </div>

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
            placeholder={
              // The ghost value is where the session is *now*. Showing the
              // last requested tempo here made the field contradict the hero
              // digits whenever Link had since agreed on something else.
              status && status.tempo_valid ? status.bpm.toFixed(1) : "120.0"
            }
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
        {/* No chips here: the sticky strip above already shows source /
            transport / network, and repeating them cost half a screen. */}
      </Card>

      {/* The switcher is the card's own header, so Sync / Set / Stats reads
          as one panel with tabs — not a second tab bar floating above the
          real one at the bottom of the phone. */}
      <section class="card">
        <div class="card__head card__head--tabs">
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
        </div>
        <div class="card__body">
          {pane === "sync" ? (
            <div class="btn-row" style="margin-top:0">
              <Button variant="secondary" onClick={() => send(() => api.resync("next"))}>
                Reset next loop
              </Button>
              <Button variant="secondary" onClick={() => send(() => api.resync("now"))}>
                Re-align grid now
              </Button>
            </div>
          ) : null}

          {pane === "set" ? (
            <>
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
            </>
          ) : null}

          {pane === "stats" ? (
            <>
              <Readout
                label="Edges emitted"
                value={<span class="mono">{status?.pulse.edges ?? "—"}</span>}
              />
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
            </>
          ) : null}
        </div>
      </section>

      {status && pane === "sync" && status.tempo_valid && status.peers === 0 && !status.setup_ap ? (
        <div class="banner">
          <div class="banner__body">
            <strong class="banner__title">{strings.states.no_link.web}</strong>
            {strings.states.no_link.long}
          </div>
        </div>
      ) : null}

      <SaveBar {...props} />
    </>
  );
}

function formatUptime(seconds: number): string {
  const h = Math.floor(seconds / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  const s = seconds % 60;
  return h > 0 ? `${h}h ${m}m` : m > 0 ? `${m}m ${s}s` : `${s}s`;
}
