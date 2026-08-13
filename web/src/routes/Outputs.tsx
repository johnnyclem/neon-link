import { useState } from "preact/hooks";
import type { PageProps } from "../app";
import type { ClockConfig, Config } from "../api";
import { stepIsOn, toggleStep } from "../api";
import { strings } from "../design/strings";
import { Card, NumberField, SelectField, Toggle } from "../components/controls";
import type { AudioRole } from "../api";
import {
  JackField,
  jackTitle,
  jackWired,
  type JackId,
} from "../components/JackField";
import { SubNav } from "../components/SubNav";
import { SaveBar } from "./SaveBar";

/**
 * The free-assignment step grid. Clicking a cell flips one bit of the
 * 64-step mask; every fourth cell is marked so the beat is findable
 * without counting.
 */
function StepGrid({
  steps,
  mask,
  onToggle,
}: {
  steps: number;
  mask: string;
  onToggle: (step: number) => void;
}) {
  return (
    <div class="steps" role="group" aria-label="Pattern steps">
      {Array.from({ length: steps }, (_, s) => (
        <button
          key={s}
          type="button"
          class={`steps__cell${stepIsOn(mask, s) ? " is-on" : ""}${
            s % 4 === 0 ? " is-beat" : ""
          }`}
          aria-pressed={stepIsOn(mask, s)}
          aria-label={`Step ${s + 1}`}
          onClick={() => onToggle(s)}
        />
      ))}
    </div>
  );
}

const AUDIO_ROLES: { value: AudioRole; label: string }[] = [
  { value: "mix", label: "Mix" },
  { value: "metronome", label: "Metronome" },
  { value: "clock", label: "Clock (audio)" },
  { value: "reset", label: "Reset (audio)" },
  { value: "run", label: "Run gate (audio)" },
  { value: "synth", label: "Synth" },
  { value: "link_in", label: "Link Audio in" },
  { value: "line_in", label: "Line in" },
];

function setClock(
  patch: PageProps["patch"],
  i: number,
  key: keyof ClockConfig,
  value: ClockConfig[keyof ClockConfig],
) {
  patch((d) => {
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    (d.engine.clocks[i] as any)[key] = value;
  });
}

/** Shape / groove / role for one engine clock (jack CLK 1 or a virtual CLK). */
function ClockEditor({
  cfg,
  index,
  patch,
  roleLabel,
}: {
  cfg: Config;
  index: number;
  patch: PageProps["patch"];
  roleLabel: string;
}) {
  const [pane, setPane] = useState<"shape" | "groove">("shape");
  const c = cfg.engine.clocks[index];

  return (
    <>
      <Toggle
        label="Enabled"
        checked={c.enabled}
        onChange={(v) => setClock(patch, index, "enabled", v)}
      />

      <div class="fields">
        <SelectField
          label={roleLabel}
          value={c.role}
          options={[
            { value: "clock", label: "Clock" },
            { value: "gate", label: "Gate while playing" },
            { value: "reset_loop", label: "Reset every loop" },
            { value: "reset_start", label: "Reset at start" },
            { value: "reset_stop", label: "Reset at stop" },
          ]}
          onChange={(v) => setClock(patch, index, "role", v)}
        />
      </div>

      {c.role === "clock" ? (
        <>
          <SubNav
            label="Clock section"
            value={pane}
            onChange={(id) => setPane(id as "shape" | "groove")}
            items={[
              { id: "shape", label: "Shape" },
              { id: "groove", label: "Groove" },
            ]}
          />

          {pane === "shape" ? (
            <>
              <div class="fields">
                <NumberField
                  label="PPQN"
                  value={c.ppqn}
                  min={1}
                  max={192}
                  onChange={(v) => setClock(patch, index, "ppqn", v)}
                />
                <NumberField
                  label="×"
                  value={c.mult}
                  min={1}
                  max={16}
                  onChange={(v) => setClock(patch, index, "mult", v)}
                />
                <NumberField
                  label="÷"
                  value={c.div}
                  min={1}
                  max={16}
                  onChange={(v) => setClock(patch, index, "div", v)}
                />
                <SelectField
                  label="Mode"
                  value={c.mode}
                  options={[
                    { value: "trig", label: "Trigger" },
                    { value: "square", label: "Square" },
                  ]}
                  onChange={(v) => setClock(patch, index, "mode", v)}
                />
                {c.mode === "trig" ? (
                  <NumberField
                    label="Trig µs"
                    value={c.trig_len_us}
                    min={1000}
                    max={100000}
                    step={1000}
                    onChange={(v) => setClock(patch, index, "trig_len_us", v)}
                  />
                ) : (
                  <NumberField
                    label="Duty %"
                    value={c.duty_pct}
                    min={1}
                    max={99}
                    onChange={(v) => setClock(patch, index, "duty_pct", v)}
                  />
                )}
              </div>
              <Toggle
                label="Free run (ignore transport stop)"
                checked={c.free_run}
                onChange={(v) => setClock(patch, index, "free_run", v)}
              />
            </>
          ) : (
            <>
              <div class="fields">
                <NumberField
                  label="Shuffle %"
                  value={c.shuffle_pct}
                  min={0}
                  max={75}
                  onChange={(v) => setClock(patch, index, "shuffle_pct", v)}
                />
                <SelectField
                  label="Pattern"
                  value={c.rhythm}
                  options={[
                    { value: "all", label: "Every pulse" },
                    { value: "euclid", label: "Euclidean" },
                    { value: "probability", label: "Chance" },
                    { value: "pattern", label: "Free steps" },
                  ]}
                  onChange={(v) => setClock(patch, index, "rhythm", v)}
                />
                {c.rhythm === "euclid" || c.rhythm === "pattern" ? (
                  <NumberField
                    label="Steps"
                    value={c.euclid_steps}
                    min={1}
                    max={64}
                    onChange={(v) => setClock(patch, index, "euclid_steps", v)}
                  />
                ) : null}
                {c.rhythm === "euclid" ? (
                  <>
                    <NumberField
                      label="Fills"
                      value={c.euclid_fills}
                      min={0}
                      max={64}
                      onChange={(v) => setClock(patch, index, "euclid_fills", v)}
                    />
                    <NumberField
                      label="Rotate"
                      value={c.euclid_rot}
                      min={0}
                      max={63}
                      onChange={(v) => setClock(patch, index, "euclid_rot", v)}
                    />
                  </>
                ) : null}
                {c.rhythm !== "all" ? (
                  <NumberField
                    label="Chance %"
                    value={c.probability_pct}
                    min={0}
                    max={100}
                    onChange={(v) => setClock(patch, index, "probability_pct", v)}
                  />
                ) : null}
                <NumberField
                  label="Jitter %"
                  value={c.humanize_pct}
                  min={0}
                  max={50}
                  onChange={(v) => setClock(patch, index, "humanize_pct", v)}
                />
              </div>
              {c.rhythm === "pattern" ? (
                <StepGrid
                  steps={c.euclid_steps}
                  mask={c.step_mask}
                  onToggle={(step) =>
                    setClock(patch, index, "step_mask", toggleStep(c.step_mask, step))
                  }
                />
              ) : null}
              {c.rhythm !== "all" ? (
                <Toggle
                  label="Steps span the loop"
                  checked={c.rhythm_over_loop}
                  onChange={(v) => setClock(patch, index, "rhythm_over_loop", v)}
                />
              ) : null}
            </>
          )}
        </>
      ) : null}
    </>
  );
}

function JackSettings({
  id,
  cfg,
  patch,
}: {
  id: JackId;
  cfg: Config;
  patch: PageProps["patch"];
}) {
  switch (id) {
    case "cv2-out":
      return (
        <>
          <p class="jack-copy">
            Physical clock / gate. This is CLK 1, wired to CV out 2.
          </p>
          <ClockEditor cfg={cfg} index={0} patch={patch} roleLabel="This jack" />
        </>
      );

    case "cv1-out":
      return (
        <>
          <p class="jack-copy">
            Tempo CV on CV out 1. Pitch mode steals the same jack for 1 V/oct
            from the last MIDI note.
          </p>
          <div class="fields">
            <SelectField
              label="This jack"
              value={cfg.ble.pitch_cv ? "pitch" : "tempo"}
              options={[
                { value: "tempo", label: "Tempo CV (0–5 V)" },
                { value: "pitch", label: "Pitch CV (1 V/oct)" },
              ]}
              onChange={(v) => patch((d) => (d.ble.pitch_cv = v === "pitch"))}
            />
          </div>
          {!cfg.ble.pitch_cv ? (
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
          ) : (
            <p class="jack-copy">
              Note pitch from BLE / TRS MIDI drives this jack. Turn pitch off
              to put the BPM range back on it.
            </p>
          )}
        </>
      );

    case "cv1-in":
      return (
        <>
          <p class="jack-copy">
            External clock in. Rising edges ≥ 1 V set the Link tempo while
            this source is active.
          </p>
          <div class="fields">
            <SelectField
              label="Source"
              value={cfg.clock_source}
              options={[
                { value: "auto", label: "Auto — CLK IN wins while patched" },
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
        </>
      );

    case "cv2-in":
      return (
        <p class="jack-copy">
          Reset in. A rising edge on this jack lines the downbeat up with the
          incoming pulse. There is nothing to assign — the hole is the reset
          input.
        </p>
      );

    case "midi-out":
      return (
        <>
          <p class="jack-copy">TRS MIDI out (Type A). Notes always leave here.</p>
          <Toggle
            label="Send MIDI clock on this jack"
            checked={cfg.ble.midi_clock_out}
            onChange={(v) => patch((d) => (d.ble.midi_clock_out = v))}
          />
          <div class="fields">
            <NumberField
              label="MIDI nudge"
              value={cfg.midi_nudge_us}
              min={-100000}
              max={100000}
              step={500}
              onChange={(v) => patch((d) => (d.midi_nudge_us = v))}
              hint="µs — MIDI only, independent of CV latency"
            />
          </div>
        </>
      );

    case "midi-in":
      return (
        <>
          <p class="jack-copy">
            TRS MIDI in. Clock and transport from a hardware box land here;
            BLE MIDI uses the same routing.
          </p>
          <div class="fields">
            <SelectField
              label="Incoming clock"
              value={cfg.ble.clock_policy}
              options={[
                { value: "ignore", label: "Ignore — Link is the timeline" },
                { value: "replace", label: "Replace the Link clock" },
                { value: "merge", label: "Merge both sources" },
              ]}
              onChange={(v) => patch((d) => (d.ble.clock_policy = v))}
            />
          </div>
          <Toggle
            label="Follow transport messages"
            checked={cfg.ble.transport_enabled}
            onChange={(v) => patch((d) => (d.ble.transport_enabled = v))}
          />
        </>
      );

    case "line-out":
      return (
        <>
          <p class="jack-copy">
            Stereo line out from the audio engine — the mix, or a solo tap of
            one source per channel. Levels and the metronome live on the{" "}
            <a href="#/audio">Audio page</a>.
          </p>
          <Toggle
            label="Audio engine enabled (takes effect on reboot)"
            checked={cfg.audio.enabled}
            onChange={(v) => patch((d) => (d.audio.enabled = v))}
          />
          <div class="fields">
            <SelectField
              label="Left carries"
              value={cfg.audio.role_l}
              options={AUDIO_ROLES}
              onChange={(v) => patch((d) => (d.audio.role_l = v))}
            />
            <SelectField
              label="Right carries"
              value={cfg.audio.role_r}
              options={AUDIO_ROLES}
              onChange={(v) => patch((d) => (d.audio.role_r = v))}
            />
          </div>
        </>
      );

    case "line-in":
      return (
        <>
          <p class="jack-copy">
            Stereo line in. Feed it into the mix here; publishing it as a Link
            Audio channel lives on the <a href="#/audio">Audio page</a>.
          </p>
          <div class="fields">
            <NumberField
              label="Monitor %"
              value={Math.round((cfg.audio.linein_monitor_gain * 100) / 200)}
              min={0}
              max={127}
              onChange={(v) =>
                patch(
                  (d) =>
                    (d.audio.linein_monitor_gain = Math.min(
                      255,
                      Math.round((v * 200) / 100),
                    )),
                )
              }
            />
          </div>
        </>
      );

    default:
      return (
        <p class="jack-copy">
          S/PDIF is on the AMYboard but deliberately deferred — the firmware
          does not drive it yet. Everything else on the block is live.
        </p>
      );
  }
}

/**
 * Jack-first outputs page. The 5×2 field is the same block as the
 * AMYboard; tap a hole to see (and, where the hardware allows, change)
 * what that socket does. CLK 2–4 stay under Virtual — they have no jack.
 */
export function Outputs(props: PageProps) {
  const { cfg, patch } = props;
  const [jack, setJack] = useState<JackId>("cv2-out");
  const [virt, setVirt] = useState(1);

  return (
    <>
      <h1 class="page-title">{strings.screens.outputs.web}</h1>

      <JackField cfg={cfg} selected={jack} onSelect={setJack} />

      <Card
        title={jackTitle(jack)}
        note={
          jackWired(jack)
            ? undefined
            : "Silk on the board, not connected in this firmware."
        }
      >
        <JackSettings id={jack} cfg={cfg} patch={patch} />
      </Card>

      <Card
        title="Virtual CLK 2–4"
        note="No extra jacks. BLE note gates can still fire these clocks."
      >
        <SubNav
          label="Virtual clock"
          value={String(virt)}
          onChange={(id) => setVirt(Number(id))}
          items={cfg.engine.clocks.slice(1).map((clock, idx) => ({
            id: String(idx + 1),
            label: clock.enabled ? `${idx + 2}` : `${idx + 2}·`,
          }))}
        />
        <ClockEditor cfg={cfg} index={virt} patch={patch} roleLabel="Role" />
      </Card>

      <SaveBar {...props} />
    </>
  );
}
