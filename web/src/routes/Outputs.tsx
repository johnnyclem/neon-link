import type { PageProps } from "../app";
import type { ClockConfig } from "../api";
import { strings } from "../design/strings";
import { Card, NumberField, SelectField, Toggle } from "../components/controls";
import { SaveBar } from "./SaveBar";

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
                  { value: "probability", label: "Probability" },
                ]}
                onChange={(v) => setClock(i, "rhythm", v)}
              />
              {c.rhythm === "euclid" ? (
                <>
                  <NumberField
                    label="Steps"
                    value={c.euclid_steps}
                    min={1}
                    max={64}
                    onChange={(v) => setClock(i, "euclid_steps", v)}
                  />
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
              {c.rhythm === "probability" ? (
                <NumberField
                  label="Probability"
                  value={c.probability_pct}
                  min={0}
                  max={100}
                  onChange={(v) => setClock(i, "probability_pct", v)}
                  hint="%"
                />
              ) : null}
              <NumberField
                label="Humanize"
                value={c.humanize_pct}
                min={0}
                max={50}
                onChange={(v) => setClock(i, "humanize_pct", v)}
                hint="%"
              />
            </div>
          </Card>
        ))}
      </div>

      <SaveBar {...props} />
    </>
  );
}
