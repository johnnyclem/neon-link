import { useState } from "preact/hooks";
import { tokens } from "../design/tokens";
import { strings, type StateName } from "../design/strings";
import { iconMasters, ICON_TICK_HZ, type IconName } from "../design/icons";
import { Icon } from "../components/Icon";
import { HeroTempo } from "../components/HeroTempo";
import { PhaseBar } from "../components/PhaseBar";
import { StatusChip } from "../components/StatusChip";
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
import { DeviceSim, compactScreens, screens } from "./DeviceSim";

function ratio(fg: string, bg: string): number {
  const lum = (hex: string) => {
    const v = hex.replace("#", "");
    const ch = [0, 2, 4].map((i) => {
      const c = parseInt(v.slice(i, i + 2), 16) / 255;
      return c <= 0.04045 ? c / 12.92 : ((c + 0.055) / 1.055) ** 2.4;
    });
    return 0.2126 * ch[0] + 0.7152 * ch[1] + 0.0722 * ch[2];
  };
  const a = lum(fg);
  const b = lum(bg);
  return (Math.max(a, b) + 0.05) / (Math.min(a, b) + 0.05);
}

function Section({ id, title, lede, children }: {
  id: string;
  title: string;
  lede?: string;
  children: preact.ComponentChildren;
}) {
  return (
    <section id={id} style="margin-bottom:var(--space-8)">
      <h2 class="page-title" style="font-size:var(--text-title)">
        {title}
      </h2>
      {lede ? <p class="page-intro">{lede}</p> : null}
      {children}
    </section>
  );
}

export function Styleguide() {
  const [scale, setScale] = useState(2);
  const [tempo, setTempo] = useState(128);
  const [phase, setPhase] = useState(0.375);
  const [running, setRunning] = useState(true);
  const [toggled, setToggled] = useState(true);
  const [modal, setModal] = useState(false);

  return (
    <main class="shell">
      <h1 class="page-title" style="margin-bottom:var(--space-2)">
        NEON LINK — Design System
      </h1>
      <p class="page-intro">
        One system, two surfaces. Everything on this page is generated from{" "}
        <code class="mono">design/</code>: the swatches from{" "}
        <code class="mono">tokens.json</code>, the status words from{" "}
        <code class="mono">strings.json</code>, the icons from{" "}
        <code class="mono">icons.txt</code>, the numerals from{" "}
        <code class="mono">fonts/hero.json</code>, and the device screens from the
        firmware's own <code class="mono">render_ui()</code>. Nothing here is drawn
        twice, which is why the two surfaces cannot drift apart.
      </p>

      <Section
        id="device"
        title="The device"
        lede="Rendered on the host through the same code the panel runs, at 1:1 pixel fidelity. This is what the 128×128 panel actually draws — not a mockup of it."
      >
        <div class="btn-row" style="margin-bottom:var(--space-4)">
          {[1, 2, 3, 4].map((s) => (
            <Button key={s} variant={scale === s ? "primary" : "secondary"} onClick={() => setScale(s)}>
              {s}×
            </Button>
          ))}
        </div>
        <div
          style={`display:grid;grid-template-columns:repeat(auto-fill,minmax(${
            128 * scale + 40
          }px,1fr));gap:var(--space-5)`}
        >
          {screens.map((s) => (
            <DeviceSim key={s.id} screen={s} scale={scale} />
          ))}
        </div>
      </Section>

      <Section
        id="device-compact"
        title="The device — compact 128×64"
        lede="The same fixtures through the design system's compact layout (native SSD1306/1309 panels, docs/DAISY.md §4): the hero, status row and phase bar re-flow into 64 rows, drawn by the same render_ui() with ui::kLayout64."
      >
        <div
          style={`display:grid;grid-template-columns:repeat(auto-fill,minmax(${
            128 * scale + 40
          }px,1fr));gap:var(--space-5)`}
        >
          {compactScreens.map((s) => (
            <DeviceSim key={s.id} screen={s} scale={scale} compact />
          ))}
        </div>
      </Section>

      <Section
        id="bridge"
        title="The bridge"
        lede="The three things both surfaces draw from one definition. The web is not imitating the panel here — it is rendering the same source."
      >
        <Card title="Tempo readout">
          <p class="card__note" style="padding:0 0 var(--space-3)">
            The same seven-segment map from <code class="mono">fonts/hero.json</code>.
            The panel lights segments; the browser additionally shows the unlit ones,
            the way an LED readout looks up close.
          </p>
          <div style="display:flex;gap:var(--space-5);align-items:flex-end;flex-wrap:wrap">
            <HeroTempo bpm={tempo} size={96} />
            <HeroTempo bpm={tempo} size={48} />
            <HeroTempo bpm={null} size={48} />
          </div>
          <div style="max-width:320px;margin-top:var(--space-4)">
            <NumberField label="Tempo" value={tempo} min={20} max={999} onChange={setTempo} />
          </div>
        </Card>

        <Card title="Phase bar">
          <p class="card__note" style="padding:0 0 var(--space-3)">
            Drawn in the device's own coordinate space — 128 units wide, the same
            inset, the same beat ticks. Stopping the transport switches the fill to
            the panel's 50% dither.
          </p>
          <PhaseBar phase={phase} quantum={4} running={running} height={40} />
          <div style="max-width:320px;margin-top:var(--space-3)">
            <NumberField
              label="Phase"
              value={Math.round(phase * 100)}
              min={0}
              max={100}
              onChange={(v) => setPhase(v / 100)}
            />
            <div style="margin-top:var(--space-2)">
              <Toggle label="Transport running" checked={running} onChange={setRunning} />
            </div>
          </div>
        </Card>

        <Card title="Status vocabulary">
          <p class="card__note" style="padding:0 0 var(--space-3)">
            Every state carries a panel abbreviation, a web label, a sentence, an icon
            and a tone — from one entry in <code class="mono">strings.json</code>. The
            panel shows the first column; the page shows the rest.
          </p>
          <table style="width:100%;border-collapse:collapse;font-size:var(--text-label)">
            <thead>
              <tr style="text-align:left;color:var(--text-muted)">
                <th style="padding:var(--space-1) 0">Panel</th>
                <th>Chip</th>
                <th>Sentence</th>
              </tr>
            </thead>
            <tbody>
              {(Object.keys(strings.states) as StateName[]).map((name) => (
                <tr key={name} style="border-top:1px solid var(--border)">
                  <td style="padding:var(--space-2) var(--space-2) var(--space-2) 0">
                    <span class="mono" style="color:var(--neon)">
                      {strings.states[name].device || "—"}
                    </span>
                  </td>
                  <td style="padding:var(--space-2) var(--space-2) var(--space-2) 0">
                    <StatusChip state={name} bpm={tempo} />
                  </td>
                  <td style="padding:var(--space-2) 0;color:var(--text-muted)">
                    {strings.states[name].long}
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        </Card>

        <Card title="Icons">
          <p class="card__note" style="padding:0 0 var(--space-3)">
            Authored once as 8×8 1-bit masters, frames and all. The web renders the
            same master as an SVG pixel grid rather than redrawing it as a vector, and
            plays the same loop — so the icon on the page is pixel-for-pixel, and
            frame-for-frame, the icon on the panel.
          </p>
          <p class="card__note" style="padding:0 0 var(--space-3)">
            <strong style="color:var(--neon)">beat</strong> loops advance once per
            musical beat and run at the tempo below;{" "}
            <strong style="color:var(--neon)">tick</strong> loops run at a fixed{" "}
            {ICON_TICK_HZ} Hz. Everything else holds still, because motion here means
            the thing is actually happening.
          </p>
          <div style="max-width:220px;margin-bottom:var(--space-4)">
            <NumberField
              label="Tempo driving the beat loops"
              value={tempo}
              min={20}
              max={999}
              onChange={setTempo}
            />
          </div>
          <div style="display:flex;gap:var(--space-5);flex-wrap:wrap">
            {(Object.keys(iconMasters) as IconName[]).map((name) => (
              <div key={name} style="text-align:center">
                <div style="display:flex;gap:var(--space-2);align-items:flex-end;color:var(--neon)">
                  <Icon name={name} size={8} beatMs={60000 / tempo} />
                  <Icon name={name} size={16} beatMs={60000 / tempo} />
                  <Icon name={name} size={32} beatMs={60000 / tempo} />
                </div>
                <div class="mono" style="margin-top:var(--space-1);font-size:var(--text-caption);color:var(--text-muted)">
                  {name}
                </div>
                <div
                  class="mono"
                  style={`font-size:var(--text-caption);color:${
                    iconMasters[name].clock === "static"
                      ? "var(--text-muted)"
                      : "var(--neon-dim)"
                  }`}
                >
                  {iconMasters[name].clock === "static"
                    ? "static"
                    : `${iconMasters[name].clock} · ${iconMasters[name].frames.length}f`}
                </div>
              </div>
            ))}
          </div>
        </Card>
      </Section>

      <Section
        id="colour"
        title="Colour"
        lede="Dark-first. Neon cyan means the system is alive and Link is present; magenta is reserved for wireless. Ratios are computed live here and asserted in CI by scripts/check_contrast.py."
      >
        <div style="display:grid;grid-template-columns:repeat(auto-fill,minmax(220px,1fr));gap:var(--space-3)">
          {Object.entries(tokens.color).map(([name, hex]) => (
            <div key={name} style="border:2px solid var(--border)">
              <div style={`background:${hex};height:56px`} />
              <div style="padding:var(--space-2)">
                <div style="font-weight:var(--weight-bold);letter-spacing:var(--tracking-token);text-transform:uppercase;font-size:var(--text-caption)">
                  {name}
                </div>
                <div class="mono" style="color:var(--text-muted);font-size:var(--text-caption)">
                  {hex} · {ratio(hex, tokens.color.bg).toFixed(2)}:1 on page
                </div>
                <div style="color:var(--text-muted);font-size:var(--text-caption);margin-top:4px">
                  {tokens.colorUse[name as keyof typeof tokens.colorUse]}
                </div>
              </div>
            </div>
          ))}
        </div>
      </Section>

      <Section
        id="controls"
        title="Controls"
        lede="Radius 0, hard rules, a neon focus ring, and a 44px minimum target. Every state that matters says what it is in words as well as in colour."
      >
        <Card title="Fields">
          <div class="fields">
            <TextField label="Network name" value="studio-2g" onChange={() => {}} hint="2.4 GHz only" />
            <TextField label="Password" type="password" value="hunter2" onChange={() => {}} />
            <NumberField label="PPQN" value={24} min={1} max={192} onChange={() => {}} />
            <SelectField
              label="Mode"
              value="trig"
              options={[
                { value: "trig", label: "Trigger" },
                { value: "square", label: "Square" },
              ]}
              onChange={() => {}}
            />
          </div>
          <div style="margin-top:var(--space-3)">
            <Toggle label="Output enabled" checked={toggled} onChange={setToggled} />
          </div>
        </Card>

        <Card title="Buttons">
          <div class="btn-row" style="margin-top:0">
            <Button>Save</Button>
            <Button variant="secondary">Recall</Button>
            <Button variant="danger" onClick={() => setModal(true)}>
              Reboot
            </Button>
            <Button disabled>Saved</Button>
          </div>
        </Card>

        <Card title="Readouts">
          <Readout label="Address" value={<span class="mono">10.0.0.42</span>} />
          <Readout label="Worst lateness" value={<span class="mono">184 µs</span>} />
          <Readout label="Uptime" value={<span class="mono">2h 14m</span>} />
        </Card>

        <div class="banner">
          <div class="banner__body">
            <strong class="banner__title">Setup mode</strong>
            The module is serving its own access point. Enter your network and press
            Save — it stays reachable at <code>http://192.168.4.1/</code> while it
            associates.
          </div>
        </div>
      </Section>

      {modal ? (
        <ConfirmModal
          title="Reboot the module?"
          body="Clock outputs stop until it comes back up."
          confirmLabel="Reboot"
          onConfirm={() => setModal(false)}
          onCancel={() => setModal(false)}
        />
      ) : null}
    </main>
  );
}
