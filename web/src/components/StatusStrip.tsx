import { strings, type StateName } from "../design/strings";
import type { Status } from "../api";
import { HeroTempo } from "./HeroTempo";
import { PhaseBar } from "./PhaseBar";
import { StatusChip } from "./StatusChip";

/** Maps a status payload onto the shared vocabulary the panel also uses. */
export function networkState(s: Status): StateName {
  if (s.setup_ap) return "net_ap";
  if (s.network === "ethernet") return "net_ethernet";
  if (s.network === "wifi") return "net_wifi";
  return "net_none";
}

/**
 * Always-visible live state: tempo, phase, transport, network, peers.
 *
 * This is the web's echo of the device's live screen. Same words, same
 * numerals, same bar proportions — opening the page while looking at the
 * module should feel like the same instrument, larger.
 */
export function StatusStrip({ status, offline }: { status: Status | null; offline: boolean }) {
  const phase = status ? status.phase_milli / (status.quantum * 1000) : 0;

  return (
    <div class="strip">
      <div class="strip__inner">
        <span class="brand">
          {strings.brand.device} <span class="brand__mark">LINK</span>
        </span>

        <HeroTempo bpm={status && status.tempo_valid ? status.bpm : null} size={44} />

        <PhaseBar
          phase={phase}
          quantum={status?.quantum ?? 4}
          running={status?.playing ?? false}
          height={20}
        />

        <div class="strip__chips">
          {offline || !status ? (
            <span class="strip__offline">
              {offline ? "OFFLINE" : "CONNECTING…"}
            </span>
          ) : (
            <>
              <StatusChip
                state={
                  status.follow_source === "audio"
                    ? "source_audio"
                    : status.ext_clock
                      ? "source_ext"
                      : "source_link"
                }
                bpm={status.bpm}
              />
              <StatusChip
                state={status.playing ? "transport_run" : "transport_stop"}
                bpm={status.playing ? status.bpm : undefined}
              />
              <StatusChip
                bpm={status.bpm}
                state={networkState(status)}
                detail={status.peers > 0 ? `${status.peers}P` : undefined}
              />
            </>
          )}
        </div>
      </div>
    </div>
  );
}
