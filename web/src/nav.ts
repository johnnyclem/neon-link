import { strings } from "./design/strings";
import type { IconName } from "./design/icons";

export const NAV = [
  { id: "live", label: strings.screens.live.web, short: "LIVE", icon: "play" as IconName },
  { id: "outputs", label: strings.screens.outputs.web, short: "OUT", icon: "run" as IconName },
  { id: "network", label: strings.screens.network.web, short: "NET", icon: "wifi-sta" as IconName },
  { id: "midi", label: strings.screens.midi.web, short: "MIDI", icon: "ble" as IconName },
  { id: "audio", label: strings.screens.audio.web, short: "AUD", icon: "link" as IconName },
  { id: "system", label: strings.screens.system.web, short: "SYS", icon: "check" as IconName },
] as const;

export type NavId = (typeof NAV)[number]["id"];
