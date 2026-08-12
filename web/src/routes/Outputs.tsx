import type { PageProps } from "../app";
import type { ClockConfig } from "../api";
import { stepIsOn, toggleStep } from "../api";
import { strings } from "../design/strings";
import { Card, NumberField, SelectField, Toggle } from "../components/controls";
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

/**
 * The four clock outputs.
 *
 * The panel can reach these too, one parameter at a time through the
 * encoder. Here they are laid out as four cards so the relationship between
 * them — which is the whole point of having four — is visible at once.
 */
export function Outputs(props: PageProps) {
  const { cfg, patch } = props;

  const setClock = (i: number, key: keyof ClockConfig, value: ClockConfig[keyof ClockConfig]) =>
    patch((d) => {
      // eslint-disable-next-line @typescript-eslint/no-explicit-any
      (d.engine.clocks[i] as any)[key] = value;
    });

  return (
    <>
      <h1 class="page-title">{strings.screens.outputs.web}</h1>
      <p class="page-intro">
        Each output divides or multiplies the shared beat grid independently. Trigger
        length applies in trig mode, duty cycle in square mode.
      </p>

      <div class="clock-grid">
        {cfg.engine.clocks.map((c, i) => (
          <Card key={i} title={`CLK ${i + 1}`}>
            <div style="margin-bottom:var(--space-3)">
              <Toggle
                label="Output enabled"
                checked={c.enabled}
                onChange={(v) => setClock(i, "enabled", v)}
              />
            </div>

            <div class="fields">
              <SelectField
                label="Role"
                value={c.role}
                options={[
                  { value: "clock", label: "Clock" },
                  { value: "gate", label: "Gate while playing" },
                  { value: "reset_loop", label: "Reset every loop" },
                  { value: "reset_start", label: "Reset at start" },
                  { value: "reset_stop", label: "Reset at stop" },
                ]}
                onChange={(v) => setClock(i, "role", v)}
                hint="Any output can take any role"
              />
            </div>

            {c.role === "clock" ? (
            <div class="fields">
              <NumberField
                label="PPQN"
                value={c.ppqn}
                min={1}
                max={192}
                onChange={(v) => setClock(i, "ppqn", v)}
                hint="Pulses per quarter note"
              />
              <NumberField
                label="Multiply"
                value={c.mult}
                min={1}
                max={16}
                onChange={(v) => setClock(i, "mult", v)}
              />
              <NumberField
                label="Divide"
                value={c.div}
                min={1}
                max={16}
                onChange={(v) => setClock(i, "div", v)}
              />
              <SelectField
                label="Mode"
                value={c.mode}
                options={[
                  { value: "trig", label: "Trigger" },
                  { value: "square", label: "Square" },
                ]}
                onChange={(v) => setClock(i, "mode", v)}
              />
              <NumberField
                label="Trig length"
                value={c.trig_len_us}
                min={1000}
                max={100000}
                step={1000}
                onChange={(v) => setClock(i, "trig_len_us", v)}
                hint="µs"
              />
              <NumberField
                label="Duty"
                value={c.duty_pct}
                min={1}
                max={99}
                onChange={(v) => setClock(i, "duty_pct", v)}
                hint="%"
              />
              <NumberField
                label="Shuffle"
                value={c.shuffle_pct}
                min={0}
                max={75}
                onChange={(v) => setClock(i, "shuffle_pct", v)}
                hint="%"
              />
            </div>
            ) : null}

            {c.role === "clock" ? (
            <div style="margin-top:var(--space-3)">
              <Toggle
                label="Free run"
                checked={c.free_run}
                onChange={(v) => setClock(i, "free_run", v)}
              />
              <p class="card__note">
                Keeps pulsing while the transport is stopped, even with clock
                gating on. Pair with a gate output for DIN-Sync style clocking.
              </p>
            </div>
            ) : null}

            {c.role === "clock" ? (
            <>
            <h3 class="field__label" style="margin:var(--space-4) 0 var(--space-2)">
              Rhythm
            </h3>
            <div class="fields">
              <SelectField
                label="Pattern"
                value={c.rhythm}
                options={[
                  { value: "all", label: "Every pulse" },
                  { value: "euclid", label: "Euclidean" },
                  { value: "probability", label: "Chance only" },
                  { value: "pattern", label: "Free steps" },
                ]}
                onChange={(v) => setClock(i, "rhythm", v)}
              />
              {c.rhythm === "euclid" || c.rhythm === "pattern" ? (
                <NumberField
                  label="Steps"
                  value={c.euclid_steps}
                  min={1}
                  max={64}
                  onChange={(v) => setClock(i, "euclid_steps", v)}
                />
              ) : null}
              {c.rhythm === "euclid" ? (
                <>
                  <NumberField
                    label="Fills"
                    value={c.euclid_fills}
                    min={0}
                    max={64}
                    onChange={(v) => setClock(i, "euclid_fills", v)}
                  />
                  <NumberField
                    label="Rotate"
                    value={c.euclid_rot}
                    min={0}
                    max={63}
                    onChange={(v) => setClock(i, "euclid_rot", v)}
                  />
                </>
              ) : null}
              {c.rhythm !== "all" ? (
                <NumberField
                  label="Chance"
                  value={c.probability_pct}
                  min={0}
                  max={100}
                  onChange={(v) => setClock(i, "probability_pct", v)}
                  hint="% that an enabled step fires"
                />
              ) : null}
              <NumberField
                label="Jitter"
                value={c.humanize_pct}
                min={0}
                max={50}
                onChange={(v) => setClock(i, "humanize_pct", v)}
                hint="%"
              />
            </div>

            {c.rhythm === "pattern" ? (
              <StepGrid
                steps={c.euclid_steps}
                mask={c.step_mask}
                onToggle={(step) =>
                  setClock(i, "step_mask", toggleStep(c.step_mask, step))
                }
              />
            ) : null}

            {c.rhythm !== "all" ? (
              <div style="margin-top:var(--space-3)">
                <Toggle
                  label="Steps span the loop"
                  checked={c.rhythm_over_loop}
                  onChange={(v) => setClock(i, "rhythm_over_loop", v)}
                />
                <p class="card__note">
                  Distributes the steps across one loop instead of the PPQN
                  grid — 16 steps across a 4-beat loop is four per beat.
                </p>
              </div>
            ) : null}
            </>
            ) : null}
          </Card>
        ))}
      </div>

      <SaveBar {...props} />
    </>
  );
}
