import { useState } from "preact/hooks";
import type { PageProps } from "../app";
import type { ScanResult } from "../api";
import { api, wifiFailHint } from "../api";
import { strings } from "../design/strings";
import { StatusChip } from "../components/StatusChip";
import { networkState } from "../components/StatusStrip";
import {
  Button,
  Card,
  NumberField,
  Readout,
  SelectField,
  TextField,
  Toggle,
} from "../components/controls";
import { SaveBar } from "./SaveBar";

/**
 * Network identity and credentials.
 *
 * This is the one screen the panel deliberately does not offer for editing —
 * entering a passphrase on a rotary encoder is a punishment — so the panel
 * shows the state read-only and points here.
 *
 * The module stores four networks and walks them in order, which is what a
 * box that travels between a studio, a rehearsal room and a stage actually
 * needs. Slot 1 is the one the setup wizard writes.
 */
export function Network(props: PageProps) {
  const { cfg, status, patch } = props;
  const hint = status ? wifiFailHint(status.wifi_fail_reason) : "";

  const [scanning, setScanning] = useState(false);
  const [scanMsg, setScanMsg] = useState("");
  const [found, setFound] = useState<ScanResult[]>([]);

  const scan = async () => {
    setScanning(true);
    setScanMsg("Scanning…");
    try {
      const nets = await api.scan();
      nets.sort((a, b) => b.rssi - a.rssi);
      setFound(nets);
      setScanMsg(
        nets.length
          ? `${nets.length} found — pick one to fill a slot.`
          : "Nothing found. The radio is 2.4 GHz only.",
      );
    } catch {
      setScanMsg("Scan failed.");
    } finally {
      setScanning(false);
    }
  };

  /** Drop a scanned network into the first free slot, never over a stored one. */
  const useNetwork = (ssid: string) => {
    const slot = cfg.wifi.networks.findIndex((n) => n.ssid === "");
    if (slot < 0) {
      setScanMsg("All four slots are full — clear one first.");
      return;
    }
    patch((d) => {
      d.wifi.networks[slot].ssid = ssid;
      d.wifi.networks[slot].pass = "";
    });
    setScanMsg(`Filled slot ${slot + 1}. Enter the password, then Save.`);
  };

  return (
    <>
      <h1 class="page-title">{strings.screens.network.web}</h1>

      {status && (status.setup_ap || status.network === "none") ? (
        <div class="banner">
          <div class="banner__body">
            <strong class="banner__title">Setup mode</strong>
            The module is serving its own access point. Enter your network below and
            press Save — it joins while this page stays up, then becomes reachable at{" "}
            <code>http://{status.hostname}/</code> from your LAN.
            <br />
            The radio is <strong>2.4 GHz only</strong>; a 5 GHz-only network will not
            appear to it. {hint}
          </div>
        </div>
      ) : null}

      <Card title="Current">
        <div class="strip__chips" style="margin-bottom:var(--space-3)">
          {status ? <StatusChip state={networkState(status)} /> : null}
        </div>
        <Readout label="Address" value={<span class="mono">{status?.ip || "—"}</span>} />
        <Readout label="Hostname" value={<span class="mono">{status?.hostname ?? "—"}</span>} />
        <Readout
          label="Trying"
          value={<span class="mono">{status?.wifi_ssid || "none"}</span>}
        />
        {status?.setup_ap ? (
          <Readout
            label="Access point"
            value={<span class="mono">{status.ap_ssid || "up"}</span>}
          />
        ) : null}
        <Readout label="Link peers" value={<span class="mono">{status?.peers ?? "—"}</span>} />
      </Card>

      <Card
        title="Stored networks"
        note="Tried in order, top first. Saving applies them live. Leave a password blank to keep the stored one; clear the name to free the slot."
        actions={
          <Button variant="secondary" onClick={() => void scan()} disabled={scanning}>
            {scanning ? "Scanning…" : "Scan"}
          </Button>
        }
      >
        {cfg.wifi.networks.map((n, i) => (
          <div key={i} class="net-slot">
            <div class="fields">
              <TextField
                label={`${i + 1} · Network name`}
                value={n.ssid}
                maxLength={32}
                onChange={(v) =>
                  patch((d) => {
                    // A different network must not inherit the old key.
                    if (d.wifi.networks[i].ssid !== v) {
                      d.wifi.networks[i].pass = "";
                    }
                    d.wifi.networks[i].ssid = v;
                  })
                }
                hint="2.4 GHz only"
              />
              <TextField
                label="Password"
                type="password"
                value={n.pass}
                maxLength={64}
                placeholder={n.has_pass ? "•••••••• (unchanged)" : "None"}
                onChange={(v) => patch((d) => (d.wifi.networks[i].pass = v))}
              />
            </div>
            <Toggle
              label="Hidden network"
              checked={n.hidden}
              onChange={(v) => patch((d) => (d.wifi.networks[i].hidden = v))}
            />
          </div>
        ))}

        <div class="fields" style="margin-top:var(--space-3)">
          <NumberField
            label="Attempts each"
            value={cfg.wifi.retries}
            min={1}
            max={10}
            onChange={(v) => patch((d) => (d.wifi.retries = v))}
            hint="Before moving to the next network"
          />
        </div>

        {scanMsg ? <p class="btn-row__msg">{scanMsg}</p> : null}
        {found.length ? (
          <div class="btn-row">
            {found.map((n) => (
              <Button
                key={n.ssid}
                variant="secondary"
                onClick={() => useNetwork(n.ssid)}
              >
                {n.ssid} · {n.rssi}dBm{n.open ? " · open" : ""}
              </Button>
            ))}
          </div>
        ) : null}
      </Card>

      <Card
        title="Access point"
        note="Other Link devices can join this network to sync. A password shorter than eight characters leaves it open, because that is all WPA2 accepts. In access point mode the editor is at http://192.168.4.1."
      >
        <div class="fields">
          <SelectField
            label="Create"
            value={cfg.ap.policy}
            options={[
              { value: "fallback", label: "When no network is reachable" },
              { value: "always", label: "Always — never join a network" },
              { value: "off", label: "Never" },
            ]}
            onChange={(v) => patch((d) => (d.ap.policy = v))}
          />
          <TextField
            label="Network name"
            value={cfg.ap.ssid}
            maxLength={32}
            placeholder={`${(cfg.device_name || "neon-link").toUpperCase()}-XXXX`}
            onChange={(v) => patch((d) => (d.ap.ssid = v))}
            hint="Blank derives it from the device name"
          />
          <TextField
            label="Password"
            type="password"
            value={cfg.ap.pass}
            maxLength={64}
            placeholder={cfg.ap.has_pass ? "•••••••• (unchanged)" : "None"}
            onChange={(v) => patch((d) => (d.ap.pass = v))}
          />
          <NumberField
            label="Channel"
            value={cfg.ap.channel}
            min={1}
            max={13}
            onChange={(v) => patch((d) => (d.ap.channel = v))}
          />
        </div>
        <Toggle
          label="Require a password"
          checked={cfg.ap.require_pass}
          onChange={(v) => patch((d) => (d.ap.require_pass = v))}
        />
        <Toggle
          label="Hidden network"
          checked={cfg.ap.hidden}
          onChange={(v) => patch((d) => (d.ap.hidden = v))}
        />
      </Card>

      <SaveBar {...props} />
    </>
  );
}
