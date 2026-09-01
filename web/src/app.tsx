import { useCallback, useEffect, useRef, useState } from "preact/hooks";

const kSetupDismissed = "neon.setupDismissed";
import { adoptConfig, api, type Config, type Status } from "./api";
import { strings } from "./design/strings";
import { StatusStrip } from "./components/StatusStrip";
import { TabBar } from "./components/TabBar";
import { NAV } from "./nav";
import { useHashRoute, useStatus } from "./useStatus";
import { Live } from "./routes/Live";
import { Outputs } from "./routes/Outputs";
import { Network } from "./routes/Network";
import { Midi } from "./routes/Midi";
import { Audio } from "./routes/Audio";
import { System } from "./routes/System";
import { Setup } from "./routes/Setup";
import { applyTheme, isThemeId } from "./theme";

export interface PageProps {
  cfg: Config;
  status: Status | null;
  patch: (mutate: (draft: Config) => void) => void;
  save: () => Promise<void>;
  dirty: boolean;
  saving: boolean;
  message: { text: string; kind: "ok" | "err" } | null;
  dismissSetup: () => void;
}

export function App() {
  const { status, offline } = useStatus();
  const [cfg, setCfg] = useState<Config | null>(null);
  const [dirty, setDirty] = useState(false);
  const [saving, setSaving] = useState(false);
  const [message, setMessage] = useState<PageProps["message"]>(null);
  const [loadError, setLoadError] = useState<string | null>(null);
  const [setupDismissed, setSetupDismissed] = useState(
    () => sessionStorage.getItem(kSetupDismissed) === "1",
  );
  const route = useHashRoute("live");

  useEffect(() => {
    api
      .getConfig()
      .then(setCfg)
      .catch((e: Error) => setLoadError(e.message));
  }, []);

  useEffect(() => {
    if (cfg && isThemeId(cfg.color_theme)) {
      applyTheme(cfg.color_theme);
    }
  }, [cfg?.color_theme]);

  const cfgRef = useRef(cfg);
  cfgRef.current = cfg;

  const patch = useCallback((mutate: (draft: Config) => void) => {
    setCfg((current) => {
      if (!current) return current;
      const draft = structuredClone(current) as Config;
      mutate(draft);
      cfgRef.current = draft;
      return draft;
    });
    setDirty(true);
    setMessage(null);
  }, []);

  const save = useCallback(async () => {
    const current = cfgRef.current;
    if (!current) return;
    setSaving(true);
    try {
      // The device answers with the sanitized config; adopting its reply is
      // how the form learns about any value it clamped. If the station
      // bounces mid-reply, the write has already landed — GET what stuck.
      let applied: Config;
      try {
        applied = await api.putConfig(current);
      } catch {
        applied = await api.getConfig();
      }
      const next = adoptConfig(applied, current);
      cfgRef.current = next;
      setCfg(next);
      setDirty(false);
      setMessage({ text: strings.states.saved.long, kind: "ok" });
    } catch (e) {
      setMessage({ text: `Could not save: ${(e as Error).message}`, kind: "err" });
    } finally {
      setSaving(false);
    }
  }, []);

  // A success confirmation has a shelf life; once read, the save bar it
  // holds open should give the row back. Errors stay until acted on.
  useEffect(() => {
    if (message?.kind !== "ok") return;
    const timer = setTimeout(() => setMessage(null), 4000);
    return () => clearTimeout(timer);
  }, [message]);

  if (loadError) {
    return (
      <>
        <StatusStrip status={status} offline={offline} />
        <main class="shell">
          <div class="banner banner--danger">
            <div class="banner__body">
              <strong class="banner__title">Could not reach the module</strong>
              {loadError}. Check that you are on the same network, then reload.
              During setup the module serves this page at{" "}
              <code>http://{strings.brand.setup_ip}/</code>.
            </div>
          </div>
        </main>
      </>
    );
  }

  if (!cfg) {
    return (
      <>
        <StatusStrip status={status} offline={offline} />
        <main class="shell">
          <p class="page-intro" style="margin-top:var(--space-5)">
            Loading configuration…
          </p>
        </main>
      </>
    );
  }

  const dismissSetup = useCallback(() => {
    sessionStorage.setItem(kSetupDismissed, "1");
    setSetupDismissed(true);
    if (window.location.hash !== "#/live") {
      window.location.hash = "#/live";
    }
  }, []);

  const props: PageProps = {
    cfg,
    status,
    patch,
    save,
    dirty,
    saving,
    message,
    dismissSetup,
  };

  // First boot lands on the wizard rather than the full editor: there is
  // exactly one thing to do at that point, and burying it in a settings page
  // is how people end up stuck on the access point. Finish dismisses it so
  // #/live is not trapped on the wizard while the setup AP is still up.
  const inSetup =
    route === "setup" ||
    (status?.setup_ap === true && route === "live" && !setupDismissed);

  return (
    <>
      <StatusStrip status={status} offline={offline} />
      <nav class="nav nav--top" aria-label="Sections">
        <div class="nav__inner">
          {NAV.map((r) => (
            <a
              key={r.id}
              class="nav__item"
              href={`#/${r.id}`}
              aria-current={route === r.id ? "page" : undefined}
            >
              {r.label}
            </a>
          ))}
        </div>
      </nav>

      <main class="shell">
        {inSetup ? <Setup {...props} /> : null}
        {!inSetup && route === "live" ? <Live {...props} /> : null}
        {route === "outputs" ? <Outputs {...props} /> : null}
        {route === "network" ? <Network {...props} /> : null}
        {route === "midi" ? <Midi {...props} /> : null}
        {route === "audio" ? <Audio {...props} /> : null}
        {route === "system" ? <System {...props} /> : null}
      </main>
      <TabBar route={inSetup ? "live" : route} />
    </>
  );
}
