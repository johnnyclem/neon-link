# NEON LINK — Ableton VST3

In-set device manager for the NEON LINK Eurorack module. Audio-effect
passthrough; talks HTTP to the module's existing REST API. Not a Link peer.

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

- Stereo passthrough. No DSP. No sockets on the audio callback.
- Does **not** vendor Ableton Link. Live is already the Link peer.
- Does **not** perform OTA, factory reset, or Wi-Fi join.
- Network bind / editor chrome ship in later PRs.

## License

This plugin is licensed under the GNU Affero General Public License
v3.0, matching JUCE 8's AGPLv3 option. The firmware's Ableton Link
dependency (GPLv2+) is **not** linked into this binary.
