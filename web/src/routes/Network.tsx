import type { PageProps } from "../app";
import { wifiFailHint } from "../api";
import { strings } from "../design/strings";
import { StatusChip } from "../components/StatusChip";
import { networkState } from "../components/StatusStrip";
import { Card, Readout, TextField } from "../components/controls";
import { SaveBar } from "./SaveBar";

/**
 * Network identity and credentials.
 *
 * This is the one screen the panel deliberately does not offer for editing —
 * entering a passphrase on a rotary encoder is a punishment — so the panel
 * shows the state read-only and points here.
 */
export function Network(props: PageProps) {
  const { cfg, status, patch } = props;
  const hint = status ? wifiFailHint(status.wifi_fail_reason) : "";

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
          label="Configured network"
          value={<span class="mono">{status?.wifi_ssid || "none"}</span>}
        />
        <Readout label="Link peers" value={<span class="mono">{status?.peers ?? "—"}</span>} />
      </Card>

      <Card
        title="Wi-Fi"
        note={
          <>
            Saving applies the credentials live. Leave the password blank to keep the
            stored one. Use Reboot on the System page only if association gets stuck.
          </>
        }
      >
        <div class="fields">
          <TextField
            label="Network name"
            value={cfg.wifi.ssid}
            maxLength={32}
            onChange={(v) => patch((d) => (d.wifi.ssid = v))}
            hint="2.4 GHz only"
          />
          <TextField
            label="Password"
            type="password"
            value={cfg.wifi.pass}
            maxLength={64}
            placeholder={
              status && status.wifi_pass_len > 0 ? "•••••••• (unchanged)" : "None"
            }
            onChange={(v) => patch((d) => (d.wifi.pass = v))}
          />
        </div>
      </Card>

      <SaveBar {...props} />
    </>
  );
}
