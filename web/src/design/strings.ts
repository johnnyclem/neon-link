// GENERATED FILE - do not edit.
// Source: design/tokens.json, design/strings.json, design/icons.txt, design/fonts/hero.json
// Regenerate: python3 scripts/gen_design.py

export const strings = {
  "vocabulary": {
    "LINK": "Ableton Link",
    "STOP": "Transport stopped",
    "RUN": "Transport running",
    "AP": "Access point mode",
    "STA": "Station mode (joined a network)",
    "BPM": "Beats per minute",
    "BLE": "Bluetooth Low Energy MIDI",
    "MIDI": "MIDI",
    "CV": "Control voltage",
    "CLK": "Clock output",
    "RST": "Reset output"
  },
  "states": {
    "source_link": {
      "device": "LINK",
      "web": "Link",
      "long": "Following the Link session",
      "icon": "link",
      "tone": "neon"
    },
    "source_ext": {
      "device": "EXT",
      "web": "External",
      "long": "Following the external clock input",
      "icon": "link",
      "tone": "yellow"
    },
    "transport_run": {
      "device": "RUN",
      "web": "Running",
      "long": "Transport running",
      "icon": "play",
      "tone": "success"
    },
    "transport_stop": {
      "device": "STOP",
      "web": "Stopped",
      "long": "Transport stopped",
      "icon": "stop",
      "tone": "text-muted"
    },
    "net_ap": {
      "device": "AP",
      "web": "Setup AP",
      "long": "Serving its own setup access point",
      "icon": "wifi-ap",
      "tone": "yellow"
    },
    "net_wifi": {
      "device": "STA",
      "web": "Wi-Fi",
      "long": "Joined a Wi-Fi network",
      "icon": "wifi-sta",
      "tone": "success"
    },
    "net_ethernet": {
      "device": "ETH",
      "web": "Ethernet",
      "long": "Connected over Ethernet",
      "icon": "wifi-sta",
      "tone": "success"
    },
    "net_none": {
      "device": "OFF",
      "web": "Offline",
      "long": "No network connection",
      "icon": "warning",
      "tone": "danger"
    },
    "ble_on": {
      "device": "BLE",
      "web": "BLE on",
      "long": "Bluetooth MIDI advertising",
      "icon": "ble",
      "tone": "magenta"
    },
    "ble_off": {
      "device": "",
      "web": "BLE off",
      "long": "Bluetooth MIDI disabled",
      "icon": "ble",
      "tone": "text-muted"
    },
    "no_link": {
      "device": "NO LINK",
      "web": "No session",
      "long": "Not connected to a Link session. Check Wi-Fi or start a session in Ableton.",
      "icon": "warning",
      "tone": "danger"
    },
    "saved": {
      "device": "SAVED",
      "web": "Saved",
      "long": "Settings saved to the device.",
      "icon": "check",
      "tone": "success"
    }
  },
  "screens": {
    "live": {
      "device": "LIVE",
      "web": "Live"
    },
    "outputs": {
      "device": "OUTPUTS",
      "web": "Outputs"
    },
    "network": {
      "device": "NETWORK",
      "web": "Network"
    },
    "midi": {
      "device": "MIDI",
      "web": "MIDI / BLE"
    },
    "audio": {
      "device": "AUDIO",
      "web": "Audio"
    },
    "system": {
      "device": "SYSTEM",
      "web": "System"
    }
  },
  "brand": {
    "device": "NEON",
    "web": "NEON LINK",
    "host": "neon-link.local",
    "setup_ip": "192.168.4.1"
  }
} as const;

export type StateName = keyof typeof strings.states;
export type ScreenName = keyof typeof strings.screens;

/** The device's word for a state - use this wherever the panel would show it. */
export const deviceWord = (name: StateName): string => strings.states[name].device;
