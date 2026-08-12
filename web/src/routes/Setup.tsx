import type { PageProps } from "../app";
import { wifiFailHint } from "../api";
import { strings } from "../design/strings";
import { Icon } from "../components/Icon";
import { Button, Card, TextField, Toggle } from "../components/controls";

/**
 * First boot.
 *
 * DESIGN_SYSTEM.md §10 calls this the strongest moment of cohesion: the
 * panel is showing the address you typed to get here, and this page mirrors
 * the panel's state back at you. So the wizard stays linear and calm and
 * does exactly one thing per step — the full editor is one tap away for
 * anyone who does not want to be walked through it.
 */
export function Setup(props: PageProps) {
  const { cfg, status, patch, save, saving, message } = props;

  const joined = status !== null && !status.setup_ap && status.network !== "none";
  const hint = status ? wifiFailHint(status.wifi_fail_reason) : "";

  return (
    <>
      <h1 class="page-title">Set up {strings.brand.web}</h1>
      <p class="page-intro">
        The module is serving this page from its own access point. Three steps and it
        is on your network — after that it is reachable at{" "}
        <code class="mono">http://{status?.hostname ?? strings.brand.host}/</code> from
        anywhere on the LAN.
      </p>

      <Card title="1 · Join your network">
        <div class="fields">
          <TextField
            label="Network name"
            value={cfg.wifi.ssid}
            maxLength={32}
            onChange={(v) => patch((d) => (d.wifi.ssid = v))}
            hint="2.4 GHz only — the radio cannot see 5 GHz networks"
          />
          <TextField
            label="Password"
            type="password"
            value={cfg.wifi.pass}
            maxLength={64}
            placeholder="None"
            onChange={(v) => patch((d) => (d.wifi.pass = v))}
          />
        </div>
        <div class="btn-row">
          <Button onClick={() => void save()} disabled={saving || cfg.wifi.ssid === ""}>
            {saving ? "Applying…" : "Join network"}
          </Button>
          {message ? (
            <span class={`btn-row__msg btn-row__msg--${message.kind === "ok" ? "ok" : "err"}`}>
              {message.text}
            </span>
          ) : null}
        </div>
      </Card>

      <Card
        title="2 · Wait for the panel"
        note="The module keeps this access point up while it associates, so you will not be cut off mid-setup."
      >
        <p style="margin:0 0 var(--space-3)">
          The display shows its network state on the same line as the tempo. When it
          reads <strong class="mono">STA</strong> with an address instead of{" "}
          <strong class="mono">AP</strong>, it has joined.
        </p>
        <div class="strip__chips">
          <span class={`chip chip--${joined ? "success" : "yellow"}`}>
            <Icon name={joined ? "check" : "wifi-ap"} size={12} />
            {joined ? `Joined — ${status?.ip}` : "Still on the setup access point"}
          </span>
        </div>
        {hint ? (
          <p class="btn-row__msg btn-row__msg--err" style="margin-top:var(--space-2)">
            {hint}
          </p>
        ) : null}
      </Card>

      <Card
        title="3 · Optional — Bluetooth MIDI"
        note="Everything here can be changed later from the MIDI / BLE page or from the module's own menu."
      >
        <Toggle
          label="Advertise as a Bluetooth MIDI device"
          checked={cfg.ble.enabled}
          onChange={(v) => patch((d) => (d.ble.enabled = v))}
        />
        <div class="btn-row">
          <Button onClick={() => void save()} disabled={saving}>
            Save
          </Button>
          <a class="btn btn--secondary" href="#/live" style="display:inline-flex;align-items:center">
            Finish
          </a>
        </div>
      </Card>
    </>
  );
}
