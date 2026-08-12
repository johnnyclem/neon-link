import { useEffect, useState } from "preact/hooks";
import { api, type Status } from "./api";

const POLL_MS = 1000;

export interface Live {
  status: Status | null;
  /** True once a poll has failed — the module went away or is rebooting. */
  offline: boolean;
}

/**
 * Polls /api/status.
 *
 * One second, not two: the strip carries a phase bar that mirrors the
 * panel's, and at two seconds it reads as broken rather than live. The
 * payload is well under a kilobyte, so this is cheap even over the setup AP.
 */
export function useStatus(): Live {
  const [status, setStatus] = useState<Status | null>(null);
  const [offline, setOffline] = useState(false);

  useEffect(() => {
    let cancelled = false;
    let timer: number;

    const tick = async () => {
      try {
        const next = await api.getStatus();
        if (cancelled) return;
        setStatus(next);
        setOffline(false);
      } catch {
        if (cancelled) return;
        setOffline(true);
      }
      if (!cancelled) timer = window.setTimeout(tick, POLL_MS);
    };

    void tick();
    return () => {
      cancelled = true;
      window.clearTimeout(timer);
    };
  }, []);

  return { status, offline };
}

/** The route the URL hash names, defaulting to the live screen. */
export function useHashRoute(fallback: string): string {
  const read = () => window.location.hash.replace(/^#\/?/, "") || fallback;
  const [route, setRoute] = useState(read);
  useEffect(() => {
    const onChange = () => setRoute(read());
    window.addEventListener("hashchange", onChange);
    return () => window.removeEventListener("hashchange", onChange);
  });
  return route;
}
