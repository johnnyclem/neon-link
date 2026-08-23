# NEON LINK — Ableton VST3

In-set device manager for the NEON LINK Eurorack module. Audio-effect
passthrough; talks HTTP to the module's REST API for configuration, and
joins the module mesh directly as a **Neon Sync** peer
(docs/NEON_SYNC.md §4.5) so the DAW's transport can drive the hardware
without a bind. Not a Link peer.

| | |
|---|---|
| Format | VST3 audio effect (macOS `arm64`, Live 12) |
| Codes | manufacturer `Neon`, plugin `Link` |
| Bundle | `com.tulipcc.neon-link` |
| License | **AGPLv3** (JUCE modules + this tree). Source is public. |

## Build

Needs CMake ≥ 3.22, a C++17 compiler, and network on first configure
(JUCE 8.0.8 is fetched by tag).

```bash
cmake -S plugin -B build-plugin -DCMAKE_BUILD_TYPE=Debug
cmake --build build-plugin -j
```

The VST3 lands in `build-plugin/NEON LINK_artefacts/…/VST3/NEON LINK.vst3`.
Local builds also copy it to `~/Library/Audio/Plug-Ins/VST3/` unless
`CI` is set in the environment.

This directory is invisible to `idf.py`. Host tests of the portable
HTTP client (`plugin/client`) run through the existing `host/` graph:

```bash
cmake -S host -B build-host -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host -j --target neon_client_tests
ctest --test-dir build-host -R neon_client_tests --output-on-failure
```

## What this binary is (and is not)

- Stereo passthrough. No DSP. No sockets on the audio callback (the
  playhead reaches the sync thread through a wait-free seqlock).
- A Neon Sync peer (`neon::client::SyncService` around the same
  `nsync::Node` the firmware runs): multicast discovery, ping/pong clock
  measurement, and last-writer-wins session state on 239.77.83.78:20809.
  While the DAW transport runs, its tempo, bar grid, and start/stop write
  the mesh (`nsync::DawFollower`); stopped, only DAW edits are sent. Both
  the peer and the "DAW drives the mesh" switch live on the Live tab and
  persist with the set. The plugin's node id sorts below every device id,
  so the laptop never becomes the session's clock reference.
- Does **not** vendor Ableton Link. Live is already the Link peer.
- Does **not** perform OTA firmware upload (use the web editor for that).
- Full editor: Live, Outputs, Network, MIDI, Audio, System, Mic — same
  module config as the web app, plus a second bind (`neon-mic.local:17001`)
  for a Neon Mic phone. Audio never goes through this plugin.

## License

This plugin is licensed under the GNU Affero General Public License
v3.0, matching JUCE 8's AGPLv3 option. The firmware's Ableton Link
dependency (GPLv2+) is **not** linked into this binary.
