import { useState } from "preact/hooks";
import type { PageProps } from "../app";
import type { ClockConfig } from "../api";
import { stepIsOn, toggleStep } from "../api";
import { strings } from "../design/strings";
import { Card, NumberField, SelectField, Toggle } from "../components/controls";
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

/**
 * One clock at a time. Four stacked cards were unreadable on a phone;
 * CLK 1–4 and Shape / Groove keep the same fields without the scroll.
 */
export function Outputs(props: PageProps) {
  const { cfg, patch } = props;
  const [clk, setClk] = useState(0);
  const [pane, setPane] = useState<"shape" | "groove">("shape");

  const setClock = (i: number, key: keyof ClockConfig, value: ClockConfig[keyof ClockConfig]) =>
    patch((d) => {
      // eslint-disable-next-line @typescript-eslint/no-explicit-any
      (d.engine.clocks[i] as any)[key] = value;
    });

  const c = cfg.engine.clocks[clk];
  const i = clk;

  return (
    <>
      <h1 class="page-title">{strings.screens.outputs.web}</h1>

      <SubNav
        label="Clock output"
        value={String(clk)}
        onChange={(id) => setClk(Number(id))}
        items={cfg.engine.clocks.map((clock, idx) => ({
          id: String(idx),
          label: clock.enabled ? `${idx + 1}` : `${idx + 1}·`,
        }))}
      />

      <Card>
        <Toggle
          label="Enabled"
          checked={c.enabled}
          onChange={(v) => setClock(i, "enabled", v)}
        />

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
                    onChange={(v) => setClock(i, "ppqn", v)}
                  />
                  <NumberField
                    label="×"
                    value={c.mult}
                    min={1}
                    max={16}
                    onChange={(v) => setClock(i, "mult", v)}
                  />
                  <NumberField
                    label="÷"
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
                  {c.mode === "trig" ? (
                    <NumberField
                      label="Trig µs"
                      value={c.trig_len_us}
                      min={1000}
                      max={100000}
                      step={1000}
                      onChange={(v) => setClock(i, "trig_len_us", v)}
                    />
                  ) : (
                    <NumberField
                      label="Duty %"
                      value={c.duty_pct}
                      min={1}
                      max={99}
                      onChange={(v) => setClock(i, "duty_pct", v)}
                    />
                  )}
                </div>
                <Toggle
                  label="Free run (ignore transport stop)"
                  checked={c.free_run}
                  onChange={(v) => setClock(i, "free_run", v)}
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
                    onChange={(v) => setClock(i, "shuffle_pct", v)}
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
                      label="Chance %"
                      value={c.probability_pct}
                      min={0}
                      max={100}
                      onChange={(v) => setClock(i, "probability_pct", v)}
                    />
                  ) : null}
                  <NumberField
                    label="Jitter %"
                    value={c.humanize_pct}
                    min={0}
                    max={50}
                    onChange={(v) => setClock(i, "humanize_pct", v)}
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
                  <Toggle
                    label="Steps span the loop"
                    checked={c.rhythm_over_loop}
                    onChange={(v) => setClock(i, "rhythm_over_loop", v)}
                  />
                ) : null}
              </>
            )}
          </>
        ) : null}
      </Card>

      <SaveBar {...props} />
    </>
  );
}
